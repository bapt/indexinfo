# indexinfo

## about

Small BSD-2 license utility to generated GNU info page index file

It reads all .info files within the directory passed in argument
Extract the index informations from the headers and regenerate the
dir file accordingly.

If there is no .info file the dir file is simply removed

## usage

	$ indexinfo /path/to/the/directory/containing/info/files/
