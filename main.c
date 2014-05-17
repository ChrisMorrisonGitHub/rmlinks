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
#include <stddef.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <pthread.h>
#include "config.h"

#define BUFF_SIZE   4096

#define SUCCESS     0
#define NONFATAL    1
#define FATAL       2

#if defined(__APPLE__) && defined(__MACH__) && !defined(_DARWIN_FEATURE_64_BIT_INODE)
#define _DARWIN_FEATURE_64_BIT_INODE
#endif

int recurse = 0;
int softlinks = 0;
char search_dir[BUFF_SIZE];
char search_file[BUFF_SIZE];
ino_t finode = 0;
dev_t fdevice = 0;
unsigned int thread_count = 0;
int down_tools = 0;

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
    char *search_ptr = NULL;
    int err_num = 0;
    
    if ((argc == 2) && (argv[1][1] == 'h'))
    {
        print_usage(1);
        return SUCCESS;
    }
    
    if ((argc == 2) && (argv[1][1] == 'v'))
    {
        print_version();
        return SUCCESS;
    }
    
    if (argc < 3)
    {
        print_usage(0);
        return FATAL;
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
                return SUCCESS;
            case 'v':
                print_version();
                return SUCCESS;
            case '?':
                fprintf(stderr, "Unknown option \'-%c\'.\n", optopt);
                return FATAL;
            default:
                print_usage(0);
                return FATAL;
        }
    }
    
    if (realpath(argv[argc - 1], search_file) == NULL)
    {
        fprintf(stderr, "Failed to resolve full path for '%s'. Please check that it exists and is accessable.\n", argv[argc - 1]);
        return FATAL;
    }
    if (realpath(argv[argc - 2], search_dir) == NULL)
    {
        fprintf(stderr, "Failed to resolve full path for '%s'. Please check that it exists and is accessable.\n", argv[argc - 2]);
        return FATAL;
    }
    
    nlen = strlen(search_dir) - 1;
    if (search_dir[nlen] == '/') search_dir[nlen] = '\0';
    
    if (stat(search_dir, &buff1) == -1)
    {
        fprintf(stderr, "rmlinks: could not stat %s (%s).\n", search_dir, strerror(errno));
        return FATAL;
    }
    
    if (S_ISDIR(buff1.st_mode) == 0)
    {
        fprintf(stderr, "rmlinks: %s is not a directory.\n", search_dir);
        return FATAL;
    }
    
    if (lstat(search_file, &buff2) == -1)
    {
        fprintf(stderr, "rmlinks: could not stat %s (%s).\n", search_file, strerror(errno));
        return FATAL;
    }
    
    if (S_ISREG(buff2.st_mode) == 0)
    {
        fprintf(stderr, "rmlinks: %s is not a regular file.\n", search_file);
        return FATAL;
    }
    
    link_count = buff2.st_nlink;
    if (link_count == 1)
    {
        fprintf(stderr, "rmlinks: there are no hardlinks to %s.\n", search_file);
        return NONFATAL;
    }
    finode = buff2.st_ino;
    fdevice = buff2.st_dev;
    
    pthread_mutex_init(&thread_count_mutex, NULL);
    pthread_mutex_init(&stdout_mutex, NULL);
    pthread_attr_setstacksize(&attr, 2097152);
    
    // Start the search.
    search_ptr = (char *)malloc(BUFF_SIZE);
    if (search_ptr == NULL)
    {
        fprintf(stderr, "rmlinks: could not allocate memory to start main worker thread.\n");
        return FATAL;
    }
    strncpy(search_ptr, search_dir, BUFF_SIZE);
    err_num = pthread_create(&master_thread, NULL, search_directory, (void *)search_ptr);
    if (errno != 0)
    {
        free(search_ptr);
        fprintf(stderr, "Failed to start the main thread (%s).\n", strerror(err_num));
        return FATAL;
    }

    for (;;)
    {
        if (down_tools == 0)
        {
            // Restat the file
            if (stat(search_file, &buff2) == -1)
            {
                fprintf(stderr, "rmlinks: could not re-stat the orignal search file.\n");
                break;
            }

            post_link_count = buff2.st_nlink;
            if (post_link_count == 1) down_tools = 1; // Set the global flag to make all the threads clean up and exit.
        }
        nanosleep((struct timespec[]){{0, 100000000}}, NULL);
        if (thread_count == 0) break;
    }
    
    pthread_attr_destroy(&attr);
    pthread_mutex_destroy(&thread_count_mutex);
    pthread_mutex_destroy(&stdout_mutex);

    // Do not count the actual file.
    link_count--;
    post_link_count--;
    fprintf(stdout, "Successfully removed %d of %d links\n", (link_count - post_link_count), link_count);
    if (post_link_count > 1) return FATAL;
    
    return SUCCESS;
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
    const char *search_path = (const char *)param;
    DIR *dir;
    char pathname[BUFF_SIZE];
    struct stat sb;
    char *filename = NULL;
    char link_target[BUFF_SIZE];
    pthread_t thread;
    struct dirent *entry = NULL;
    struct dirent *result = NULL;
    char *search_ptr = NULL;
    int err_num = 0;
    
    if (down_tools == 1)
    {
        free(param);
        return NULL;
    }
    
    entry = (struct dirent *)malloc(offsetof(struct dirent, d_name) + pathconf(search_path, _PC_NAME_MAX) + 1);
    if (entry == NULL)
    {
        free(param);
        pthread_mutex_lock(&stdout_mutex);
        fprintf(stderr, "rmlinks: failed to allocate memory");
        pthread_mutex_unlock(&stdout_mutex);
        pthread_exit(NULL);
        return NULL;
    }

    pthread_mutex_lock(&thread_count_mutex);
    thread_count++;
    pthread_mutex_unlock(&thread_count_mutex);

    dir = opendir(search_path);
    if (dir != NULL)
    {
        while ((readdir_r(dir, entry, &result) == 0) && result != NULL)
        {
            // Check the global kill switch.
            if (down_tools == 1)
            {
                free(param);
                free(entry);
                pthread_mutex_lock(&thread_count_mutex);
                thread_count--;
                pthread_mutex_unlock(&thread_count_mutex);
                pthread_exit(NULL);
                return NULL;
            }
            filename = entry->d_name;
            if ((strcmp(filename, ".") == 0) || (strcmp(filename, "..") == 0)) continue;
            snprintf(pathname, BUFF_SIZE, "%s/%s", search_path, filename);
            
            if (lstat(pathname, &sb) == -1)
            {
                pthread_mutex_lock(&stdout_mutex);
                fprintf(stderr, "rmlinks: failed to stat %s (%s).\n", pathname, strerror(errno));
                pthread_mutex_unlock(&stdout_mutex);
                continue;
            }
            if ((S_ISDIR(sb.st_mode) != 0) && (recurse == 1) && (down_tools == 0))
            {
                search_ptr = (char *)malloc(BUFF_SIZE);
                if (search_ptr == NULL)
                {
                    pthread_mutex_lock(&stdout_mutex);
                    fprintf(stderr, "rmlinks: could not allocate memory to start worker thread for directory %s\n", pathname);
                    pthread_mutex_unlock(&stdout_mutex);
                }
                strncpy(search_ptr, pathname, BUFF_SIZE);
                err_num = pthread_create(&thread, NULL, search_directory, (void *)search_ptr);
                if (err_num != 0)
                {
                    free(search_ptr);
                    pthread_mutex_lock(&stdout_mutex);
                    fprintf(stderr, "rmlinks: failed start worker thread for directory %s (%s)\n", pathname, strerror(err_num));
                    pthread_mutex_unlock(&stdout_mutex);
                }
                continue;
            }
            if ((S_ISREG(sb.st_mode) != 0) && (sb.st_ino == finode) && (sb.st_dev == fdevice))
            {
                if (strcmp(pathname, search_file) != 0)
                {
                    if (unlink(pathname) != 0)
                    {
                        pthread_mutex_lock(&stdout_mutex);
                        fprintf(stderr, "rmlinks: failed to unlink %s (%s).\n", pathname, strerror(errno));
                        pthread_mutex_unlock(&stdout_mutex);
                    }
                    else
                    {
                        pthread_mutex_lock(&stdout_mutex);
                        fprintf(stdout, "Unlinked %s\n", pathname);
                        pthread_mutex_unlock(&stdout_mutex);
                    }
                }
                continue;
            }
            if ((S_ISLNK(sb.st_mode) != 0) && (softlinks == 1))
            {
                memset(link_target, 0, BUFF_SIZE);
                if (realpath(pathname, link_target) == NULL) continue;
                
                if (strcmp(link_target, search_file) == 0)
                {
                    if (unlink(pathname) != 0)
                    {
                        pthread_mutex_lock(&stdout_mutex);
                        fprintf(stderr, "rmlinks: failed to remove symbolic link %s (%s).\n", pathname, strerror(errno));
                        pthread_mutex_unlock(&stdout_mutex);
                    }
                    else
                    {
                        pthread_mutex_lock(&stdout_mutex);
                        fprintf(stdout, "Removed symbolic link %s\n", pathname);
                        pthread_mutex_unlock(&stdout_mutex);
                    }
                }
                continue;
            }
        }
        closedir(dir);
    }
    else
    {
        pthread_mutex_lock(&stdout_mutex);
        fprintf(stderr, "rmlink: could not search %s (%s).\n", search_path, strerror(errno));
        pthread_mutex_unlock(&stdout_mutex);
    }
    
    pthread_mutex_lock(&thread_count_mutex);
    thread_count--;
    pthread_mutex_unlock(&thread_count_mutex);
    
    free(entry);
    free(param);
    pthread_exit(NULL);
    return NULL;
}


