/*-
 * Copyright (c) 2014 Baptiste Daroussin <bapt@FreeBSD.org>
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

#define _WITH_DPRINTF
#define _WITH_GETLINE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
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

static void
parse_info_file(int fd)
{
	FILE *fp;
	char *line = NULL;
	const char *walk;
	size_t linecap = 0;
	ssize_t linelen;
	struct section *s = NULL;
	bool entries = false;
	int i;

	if ((fp = fdopen(fd, "r")) == NULL) {
		warn("Impossible to read info file");
		return;
	}

	while ((linelen = getline(&line, &linecap, fp)) > 0) {
		if (line[linelen - 1] == '\n')
			line[linelen - 1 ] = '\0';

		if (*line == '\037')
			break;

		if (strncmp(line, "INFO-DIR-SECTION ", 17) == 0) {
			s = NULL;
			walk = line;
			walk+=17;
			while (isspace(*walk))
				walk++;
			for (i = 0; i < sectionlen; i++) {
				if (strcmp(walk, sections[i]->name) == 0) {
					s = sections[i];
				}
			}

			if (s == NULL) {
				s = calloc(1, sizeof(struct section));
				s->name = strdup(walk);

				if (sectionlen + 1 > sectioncap) {
					sectioncap += 100;
					sections = reallocf(sections, sectioncap * sizeof(struct sections **));
				}

				sections[sectionlen++] = s;
			}
		}

		if (strcmp(line, "START-INFO-DIR-ENTRY") == 0) {
			entries = true;
		}

		if (strcmp(line, "END-INFO-DIR-ENTRY") == 0) {
			entries = false;
		}

		if (entries && *line == '*' && s != NULL) {
			if (s->entrieslen + 1 > s->entriescap) {
				s->entriescap += 100;
				s->entries = reallocf(s->entries, s->entriescap * sizeof(char **));
			}
			s->entries[s->entrieslen++] = strdup(line);
		}
	}

	free(line);
	fclose(fp);

	return;
}

static void
parse_info_dir(int fd)
{
	DIR *d;
	struct dirent *dp;
	const char *ext;
	int ffd;

	if ((d = fdopendir(dup(fd))) == NULL)
		err(EXIT_FAILURE, "Impossible to open directory");

	while ((dp = readdir(d)) != NULL) {
		if (dp->d_namlen < 5)
			continue;
		if ((ext = strrchr(dp->d_name, '.')) == NULL)
			continue;
		if (strcmp(ext, ".info") != 0)
			continue;

		if ((ffd = openat(fd, dp->d_name, O_RDONLY)) == -1) {
			warn("Skipping: %s", dp->d_name);
			continue;
		}

		parse_info_file(ffd);
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
generate_index(fd)
{
	int i;
	int ffd;

	unlinkat(fd, "dir", 0);

	if (sectionlen == 0)
		return;

	if ((ffd = openat(fd, "dir", O_WRONLY|O_CREAT, 0644)) == -1)
		err(EXIT_FAILURE, "Imporssible to write the index file");

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

	if (argc != 2)
		errx(EXIT_FAILURE, "Usage: indexinfo <infofilesdirectori>");

	if ((fd = open(argv[1], O_RDONLY|O_DIRECTORY)) == -1)
		err(EXIT_FAILURE, "Impossible to open %s", argv[1]);

	parse_info_dir(fd);
	generate_index(fd);

	close(fd);

	return (EXIT_SUCCESS);
}
