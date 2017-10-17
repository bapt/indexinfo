/*-
 * Copyright (c) 2014-2017 Baptiste Daroussin <bapt@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>

#ifdef HAVE_CAPSICUM
#include <sys/capsicum.h>
#include <nl_types.h>
#endif

#include <zlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fnmatch.h>
#include <fcntl.h>
#include <errno.h>
#include <err.h>
#include <dirent.h>
#include <ctype.h>

struct section {
	char *name;
	char **entries;
	size_t entriescap;
	ssize_t entrieslen;
};

struct section **sections = NULL;
size_t sectioncap = 0;
ssize_t sectionlen = 0;

static ssize_t
gzgetline(char ** restrict linep, size_t *restrict linecapp,
    gzFile restrict stream)
{
	char *buf = *linep;
	ssize_t len;
	size_t offset = 0;
	int status;

	if (buf != NULL) {
		len = *linecapp;
	} else {
		len = 100;
		buf = calloc(len, sizeof(char *));
		if (buf == NULL)
			return (-1);
	}

	for (;;) {
		if (! gzgets(stream, buf + offset, len - offset)) {
			status = 0;
			gzerror(stream, &status);
			if (status == Z_OK)
				goto ok;
			/* meh error */
			return (-1);
		}

		offset += strlen(buf + offset);

		if (buf[offset - 1] == '\n')
			goto ok;

		len *= 2;
		buf = realloc(buf, len * sizeof(char *));
		if (buf == NULL)
			return (-1);
	}

ok:
	*linep = buf;
	*linecapp = len;

	return (offset);
}

static bool
do_parse(char *line, size_t linelen, struct section **s, bool *entries)
{
	const char *walk;
	int i;

	if (line[linelen - 1] == '\n')
		line[linelen - 1 ] = '\0';

	if (*line == '\037')
		return (false);

	if (strncmp(line, "INFO-DIR-SECTION ", 17) == 0) {
		*s = NULL;
		walk = line;
		walk+=17;
		while (isspace(*walk))
			walk++;
		for (i = 0; i < sectionlen; i++) {
			if (strcmp(walk, sections[i]->name) == 0) {
				*s = sections[i];
			}
		}

		if (*s == NULL) {
			*s = calloc(1, sizeof(struct section));
			(*s)->name = strdup(walk);

			if (sectionlen + 1 > sectioncap) {
				sectioncap += 100;
				sections = realloc(sections,
				    sectioncap * sizeof(struct sections **));
			}

			sections[sectionlen++] = *s;
		}
	}

	if (strcmp(line, "START-INFO-DIR-ENTRY") == 0) {
		*entries = true;
	}

	if (strcmp(line, "END-INFO-DIR-ENTRY") == 0) {
		*entries = false;
	}

	if (*entries && *line == '*' && s != NULL) {
		if ((*s)->entrieslen + 1 > (*s)->entriescap) {
			(*s)->entriescap += 100;
			(*s)->entries = realloc((*s)->entries,
			    (*s)->entriescap * sizeof(char **));
		}
		(*s)->entries[(*s)->entrieslen++] = strdup(line);
	}
	return (true);
}

static void
parse_info_file(int fd, bool gzip)
{
	FILE *fp;
	gzFile zfp;
	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	struct section *s = NULL;
	bool entries;

	if (gzip) {
		if ((zfp = gzdopen(fd, "r")) == NULL) {
			warn("Impossible to read info file");
			return;
		}
		while ((linelen = gzgetline(&line, &linecap, zfp)) > 0) {
			if (!do_parse(line, linelen, &s, &entries))
				break;
		}
		gzclose(zfp);
	} else {
		if ((fp = fdopen(fd, "r")) == NULL) {
			warn("Impossible to read info file");
			return;
		}
		while ((linelen = getline(&line, &linecap, fp)) > 0) {
			if (!do_parse(line, linelen, &s, &entries))
				break;
		}
		fclose(fp);
	}
	free(line);

	return;
}

static void
parse_info_dir(int fd)
{
	DIR *d;
	struct dirent *dp;
	const char *ext;
	int ffd;
	bool gzip;

	if ((d = fdopendir(dup(fd))) == NULL)
		err(EXIT_FAILURE, "Impossible to open directory");

	while ((dp = readdir(d)) != NULL) {
#ifdef __linux__
		if (_D_EXACT_NAMLEN(dp) < 5)
			continue;
#else
		if (dp->d_namlen < 5)
			continue;
#endif
		gzip = false;
		if (fnmatch("*.info", dp->d_name, 0) == FNM_NOMATCH) {
			if (fnmatch("*.info.gz", dp->d_name, 0) == FNM_NOMATCH)
				continue;
			gzip = true;
		}

		if ((ffd = openat(fd, dp->d_name, O_RDONLY)) == -1) {
			warn("Skipping: %s", dp->d_name);
			continue;
		}

		parse_info_file(ffd, gzip);
		close (ffd);
	}

	closedir(d);
}

static void
print_section(struct section *s, int fd)
{
	int i;

	dprintf(fd, "\n%s\n", s->name);
	for (i = 0; i < s->entrieslen; i++) {
		dprintf(fd, "%s\n", s->entries[i]);
		free(s->entries[i]);
	}
	free(s->entries);
	free(s->name);
}

const char msg[] = ""
"  This (the Directory node) gives a menu of major topics.\n"
"  Typing \"q\" exits, \"?\" lists all Info commands, \"d\" returns here,\n"
"  \"h\" gives a primer for first-timers,\n"
"  \"mXXX<Return>\" visits the XXX manual, etc.\n";

static void
generate_index(int fd)
{
	int i;
	int ffd;

	if (sectionlen == 0) {
        if (unlinkat(fd, "dir", 0) == -1 && errno != ENOENT)
            err(EXIT_FAILURE, "Impossible to remove empty index file");
		return;
    }

	if ((ffd = openat(fd, "dir", O_WRONLY|O_CREAT|O_TRUNC, 0644)) == -1)
		err(EXIT_FAILURE, "Impossible to write the index file");

	dprintf(ffd, "Produced by: "PACKAGE_NAME" "PACKAGE_VERSION".\n");
	dprintf(ffd, "\037\nFile: dir,	Node: Top	This is the top of the INFO tree\n\n");
	dprintf(ffd, "%s\n", msg);

	dprintf(ffd, "* Menu:\n");
	for (i = 0; i < sectionlen; i++) {
		print_section(sections[i], ffd);
		free(sections[i]);
	}
	free(sections);

	close(ffd);
}

int
main(int argc, char **argv)
{
	int fd;
#ifdef HAVE_CAPSICUM
	cap_rights_t rights;
#endif

	if (argc != 2)
		errx(EXIT_FAILURE, "Usage: indexinfo <infofilesdirectory>");

	if ((fd = open(argv[1], O_DIRECTORY)) == -1)
		err(EXIT_FAILURE, "Impossible to open %s", argv[1]);

#ifdef HAVE_CAPSICUM
	cap_rights_init(&rights, CAP_READ, CAP_WRITE, CAP_FSTATFS, CAP_FSTATAT, CAP_FCNTL, CAP_CREATE, CAP_SEEK, CAP_UNLINKAT, CAP_FTRUNCATE);
	if (cap_rights_limit(fd, &rights) < 0 && errno != ENOSYS) {
		warn("cap_rights_limit() failed");
		close(fd);
		return (EXIT_FAILURE);
	}

	catopen("libc", NL_CAT_LOCALE);
	if (cap_enter() < 0 && errno != ENOSYS) {
		warn("cap_enter() failed");
		close(fd);
		return (EXIT_FAILURE);
	}
#endif
	parse_info_dir(fd);
	generate_index(fd);

	close(fd);

	return (EXIT_SUCCESS);
}
