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
#include <pthread.h>
#include "config.h"

#define BUFF_SIZE 4096

int recurse = 0;
int softlinks = 0;
char search_dir[BUFF_SIZE];
char filename[BUFF_SIZE];
ino_t finode = 0;
unsigned int thread_count = 0;

pthread_mutex_t thread_count_mutex;
pthread_mutex_t stdout_mutex;
pthread_attr_t attr;

void print_usage(int help);
void print_version(void);
void *search_directory(void *param);

int main(int argc, char *argv[])
{
    int c;
    struct stat buff1;
    struct stat buff2;
    size_t nlen = 0;
    pthread_t master_thread;
    int link_count = 0;
    int post_link_count = 0;
    
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
        return 1;
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
                return 1;
        }
    }
    
    if (realpath(argv[argc - 1], filename) == NULL)
    {
        fprintf(stderr, "Failed to resolve full path for '%s'. Please check that it exists and is accessable.\n", argv[argc - 1]);
        return 1;
    }
    if (realpath(argv[argc - 2], search_dir) == NULL)
    {
        fprintf(stderr, "Failed to resolve full path for '%s'. Please check that it exists and is accessable.\n", argv[argc - 2]);
        return 1;
    }
    
    nlen = strlen(search_dir) - 1;
    if (search_dir[nlen] == '/') search_dir[nlen] = '\0';
    
    if (stat(search_dir, &buff1) == -1)
    {
        fprintf(stderr, "rmlinks: could not stat %s (%s).\n", search_dir, strerror(errno));
        return 1;
    }
    
    if (S_ISDIR(buff1.st_mode) == 0)
    {
        fprintf(stderr, "rmlinks: %s is not a directory.\n", search_dir);
        return 1;
    }
    
    if (lstat(filename, &buff2) == -1)
    {
        fprintf(stderr, "rmlinks: could not stat %s (%s).\n", filename, strerror(errno));
        return 1;
    }
    
    if (S_ISREG(buff2.st_mode) == 0)
    {
        fprintf(stderr, "rmlinks: %s is not a regular file.\n", filename);
        return 1;
    }
    
    link_count = buff2.st_nlink;
    if (link_count == 1)
    {
        fprintf(stderr, "rmlinks: there are no hardlinks to %s.\n", filename);
        return 1;
    }
    finode = buff2.st_ino;
    
    pthread_mutex_init(&thread_count_mutex, NULL);
    pthread_mutex_init(&stdout_mutex, NULL);
    pthread_attr_setstacksize(&attr, 2097152);
    
    // Start the search.
    pthread_create(&master_thread, &attr, search_directory, search_dir);
    
    while (thread_count > 0)
    {
        sleep(1);
    }
    
    pthread_attr_destroy(&attr);
    pthread_mutex_destroy(&thread_count_mutex);
    pthread_mutex_destroy(&stdout_mutex);
    
    // Restat the file
    if (lstat(filename, &buff2) == -1)
    {
        fprintf(stderr, "rmlinks: could not re-stat %s (%s).\n", filename, strerror(errno));
        return 1;
    }
    post_link_count = buff2.st_nlink;
    
    fprintf(stdout, "Successfully removed %d of %d links\n", post_link_count, link_count);
    if (post_link_count > 1) return 1;
    
    return 0;
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

void *search_directory(void *param)
{
    char *dir_path = (char *)param;
    DIR *dir;
    struct dirent *ent;
    char path[4096];
    struct stat sb;
    char *d_name = NULL;
    char buffer[4096];
    pthread_t thread;
    
    pthread_mutex_lock(&thread_count_mutex);
    thread_count++;
    pthread_mutex_unlock(&thread_count_mutex);
    
    dir = opendir(dir_path);
    if (dir != NULL)
    {
        while ((ent = readdir(dir)) != NULL)
        {
            d_name = ent->d_name;
            if ((strcmp(d_name, ".") == 0) || (strcmp(d_name, "..") == 0)) continue;
            snprintf(path, 4096, "%s/%s", dir_path, d_name);
            
            if (lstat(path, &sb) == -1) continue;
            if ((S_ISDIR(sb.st_mode) != 0) && (recurse == 1))
            {
                pthread_create(&thread, &attr, search_directory, path);
            }
            if ((S_ISREG(sb.st_mode) != 0) && (sb.st_ino == finode))
            {
                if (strcmp(path, filename) != 0)
                {
                    if (unlink(path) != 0)
                    {
                        pthread_mutex_lock(&stdout_mutex);
                        fprintf(stderr, "rmlinks: failed to unlink %s (%s).\n", path, strerror(errno));
                        pthread_mutex_unlock(&stdout_mutex);
                    }
                    else
                    {
                        pthread_mutex_lock(&stdout_mutex);
                        fprintf(stdout, "Unlinked %s\n", path);
                        pthread_mutex_unlock(&stdout_mutex);
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
                        pthread_mutex_lock(&stdout_mutex);
                        fprintf(stderr, "rmlinks: failed to remove symbolic link %s (%s).\n", path, strerror(errno));
                        pthread_mutex_unlock(&stdout_mutex);
                    }
                    else
                    {
                        pthread_mutex_lock(&stdout_mutex);
                        fprintf(stdout, "Removed symbolic link %s\n", path);
                        pthread_mutex_unlock(&stdout_mutex);
                    }
                }
            }
        }
        closedir(dir);
    }
    else
    {
        pthread_mutex_lock(&stdout_mutex);
        fprintf(stderr, "rmlink: could not search %s (%s).\n", dir_path, strerror(errno));
        pthread_mutex_unlock(&stdout_mutex);
    }
    
    pthread_mutex_lock(&thread_count_mutex);
    thread_count--;
    pthread_mutex_unlock(&thread_count_mutex);
    
    pthread_exit(NULL);
    return NULL;
}


