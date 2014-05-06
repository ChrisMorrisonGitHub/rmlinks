/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*-  */
/*
 * main.c
 * Copyright (C) 2014 Chris Morrison <chris-morrison@cyberservices.com>
 * 
 * rmlinks is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * rmlinks is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include "config.h"

#define BUFF_SIZE 4096

int recurse = 0;
int softlinks = 0;
char search_dir[BUFF_SIZE];
char filename[BUFF_SIZE];
ino_t finode = 0;
int retval = 0;

void print_usage(int help);
void print_version(void);
void search_directory(const char *dir_path);

int main(int argc, char *argv[])
{
    int c;
    struct stat buff1;
    struct stat buff2;
    size_t nlen = 0;

    if ((argc == 2) && (argv[1][1] == 'h'))
    {
        print_usage(1);
        return 0;
    }

    if ((argc == 2) && (argv[1][1] == 'v'))
    {
        print_version();
        return 0;
    }

    if (argc < 3)
    {
        print_usage(0);
        return 2;
    }

    while ((c = getopt(argc, argv, "rshv")) != -1)
    {
        switch (c)
        {
            case 'r':
                recurse = 1;
                break;
            case 's':
                softlinks = 1;
                break;
            case 'h':
                print_usage(1);
                return 0;
            case 'v':
                print_version();
                return 0;
            case '?':
                fprintf(stderr, "Unknown option \'-%c\'.\n", optopt);
                return 2;
            default:
                print_usage(0);
                return 2;
        }
    }

    if (realpath(argv[argc - 1], filename) == NULL)
    {
        fprintf(stderr, "An unexpected error occurred.\n");
        return 2;
    }
    if (realpath(argv[argc - 2], search_dir) == NULL)
    {
        fprintf(stderr, "An unexpected error occurred.\n");
        return 2;
    }

    nlen = strlen(search_dir) - 1;
    if (search_dir[nlen] == '/') search_dir[nlen] = '\0';

    if (stat(search_dir, &buff1) == -1)
    {
        fprintf(stderr, "rmlinks: could not stat %s (%s).\n", search_dir, strerror(errno));
        return 2;
    }

    if (S_ISDIR(buff1.st_mode) == 0)
    {
        fprintf(stderr, "rmlinks: %s is not a directory.\n", search_dir);
        return 2;
    }

    if (lstat(filename, &buff2) == -1)
    {
        fprintf(stderr, "rmlinks: could not stat %s (%s).\n", filename, strerror(errno));
        return 2;
    }

    if (S_ISREG(buff2.st_mode) == 0)
    {
        fprintf(stderr, "rmlinks: %s is not a regular file.\n", filename);
        return 2;
    }

    if (buff2.st_nlink == 1)
    {
        fprintf(stderr, "rmlinks: there are no hardlinks to %s.\n", filename);
        return 2;
    }

    finode = buff2.st_ino;

    search_directory(search_dir);

    return retval;
}

void print_usage(int help)
{
    fprintf(stdout, "Usage: rmlinks [options] DIRECTORY FILE\n");

    if (help == 1)
    {
        fprintf(stdout, "\nSearch DIR for hard links to FILE and remove them.\n");
        fprintf(stdout, "  -r           Recurse sub-directories while searching.\n");
        fprintf(stdout, "  -s           Remove symbolic links to FILE as well as hard links.\n");
        fprintf(stdout, "\nDIR must be a directory and FILE must be a regular file.\n");
    }
}

void print_version(void)
{
    printf("%s version %s\nCopyright (C) 2014 Chris Morrison\n", PACKAGE, VERSION);
    printf("License GPLv3+: GNU GPL version 3 or later <https://www.gnu.org/licenses/gpl-3.0.html>\nThis is free software: you are free to change and redistribute it.\nThere is NO WARRANTY, to the extent permitted by law.");
    printf("\n\nWritten by Chris Morrison <chris-morrison@cyberservices.com>\n");
}

void search_directory(const char *dir_path)
{
    DIR *dir;
    struct dirent *ent;
    char path[4096];
    struct stat sb;
    char *d_name = NULL;
    char buffer[4096];

    dir = opendir(dir_path);
    if (dir != NULL)
    {
        while ((ent = readdir(dir)) != NULL)
        {
            d_name = ent->d_name;
            if ((strcmp(d_name, ".") == 0) || (strcmp(d_name, "..") == 0)) continue;
            snprintf(path, 4096, "%s/%s", dir_path, d_name);

            if (lstat(path, &sb) == -1) continue;
            if ((S_ISDIR(sb.st_mode) != 0) && (recurse == 1)) search_directory(path);
            if ((S_ISREG(sb.st_mode) != 0) && (sb.st_ino == finode))
            {
                if (strcmp(path, filename) != 0)
                {
                    if (unlink(path) != 0)
                    {
                        fprintf(stderr, "rmlinks: failed to unlink %s (%s).\n", path, strerror(errno));
                        retval = 1;
                    }
                    else
                    {
                        printf("Unlinked %s\n", path);
                    }
                }
            }
            if ((S_ISLNK(sb.st_mode) != 0) && (softlinks == 1))
            {
                memset(buffer, 0, 4096);
                if (realpath(path, buffer) == NULL) continue;

                if (strcmp(buffer, filename) == 0)
                {
                    if (unlink(path) != 0)
                    {
                        fprintf(stderr, "rmlinks: failed to remove symbolic link %s (%s).\n", path, strerror(errno));
                        retval = 1;
                    }
                    else
                    {
                        printf("Removed symbolic link %s\n", path);
                    }
                }
            }
        }
        closedir(dir);
    }
    else
    {
        fprintf(stderr, "rmlink: could not search %s (%s).\n", dir_path, strerror(errno));

        if (strcmp(dir_path, search_dir) == 0)
            retval = 2; /* If there was an error searching the root then it was a non-starter and we should return 2 = fatal error. */
        else
            retval = 1;
    }
}


