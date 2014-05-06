#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <ctype.h>
#include <errno.h>
#include <curl/curl.h>
#include <pcre.h>

#define OVECCOUNT		30
#define DEFAULT_MIN_SIZE	0
#define DEFAULT_MAX_SIZE	2000000
#define CONTENT_MAX_SIZE	2000000
#define DEFAULT_MODE		0755

struct memory_holder {
	char  *memory;
	size_t size;
};

void	usage(const char *, char *);
void	display_usage(const char *, int);
int		read_file(const char *, char *);
size_t	memory_write_callback(void *, size_t, size_t, void *);
int		get_file_remote(CURL *, const char *, struct memory_holder *);
int		mkdir_recursive(const char *, mode_t);
int 	do_mkdir(const char *, mode_t);
int		save_img(const char *, const char *, struct memory_holder);


int main(int argc, char **argv) {

	pcre *re;
	const char *error;
	char  pattern[80];
	char  extension_list[40];
	char *subject;
	int   erroffset;
	int   ovector[OVECCOUNT];
	int   subject_length;
	int   rc, i, j;
	char *link_start, *name_start, *extension_start;
	int   link_length, name_length, extension_length, start_offset;

	char url[1024];
	int  verbose = 0;
	int  min_size = DEFAULT_MIN_SIZE, max_size = DEFAULT_MAX_SIZE;
	int  force = 0;
	int  dir_created = 0;
	struct stat st;

	int  fd;
	int  file_size;
	char directory[200];
	char img_url[1024];
	char img_name[80];
	char html[400000];
	struct memory_holder content;

	CURL *curl_handle;

	url[0]            = 0;
	pattern[0]        = 0;
	extension_list[0] = 0;
	directory[0]      = 0;
	img_url[0]        = 0;
	img_name[0]       = 0;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-d") == 0) {
			i++;
			if (i < argc-1) {
				if (argv[i][0] == '/') {
					strcpy(directory, argv[i]);
				}
				else {
					strcpy(directory, getenv("PWD"));
					strcat(directory, "/");
					strcat(directory, argv[i]);
				}
			}
			else {
				display_usage(argv[0], 1);
			}
		}
		else if (strcmp(argv[i], "-f") == 0) {
			if (i < argc-1) {
				force = 1;
			}
			else {
				display_usage(argv[0], 1);
			}
		}
		else if (strcmp(argv[i], "-t") == 0) {
			i++;
			if (i < argc-1) {
				strcpy(extension_list, "(");
				strcat(extension_list, argv[i]);
				if (!isalpha(extension_list[1]) && !isdigit(extension_list[1])) {
					fprintf(stderr, "Error: invalid input extension list\n");
					exit(1);
				}
				j = 2;
				while (extension_list[j] != 0) {
					if (extension_list[j] == ',')
						extension_list[j] = '|';
					else if (!isalpha(extension_list[j]) && !isdigit(extension_list[j])) {
						fprintf(stderr, "Error: invalid input extension list\n");
						exit(1);
					}
					j++;
				}
				if (extension_list[j-1] == '|') {
					fprintf(stderr, "Error: invalid input extension list\n");
					exit(1);
				}
				strcat(extension_list, ")");
			}
			else {
				display_usage(argv[0], 1);
			}
		}
		else if (strcmp(argv[i], "-m") == 0) {
			i++;
			if (i < argc-1) {
				min_size = atoi(argv[i]);
			}
			else {
				display_usage(argv[0], 1);
			}
		}
		else if (strcmp(argv[i], "-M") == 0) {
			i++;
			if (i < argc-1) {
				max_size = atoi(argv[i]);
			}
			else {
				display_usage(argv[0], 1);
			}
		}
		else if (strcmp(argv[i], "-v") == 0) {
			if (i < argc-1) {
				verbose = 1;
			}
			else {
				display_usage(argv[0], 1);
			}
		}
		else if (strcmp(argv[i], "-h") == 0) {
			display_usage(argv[0], 0);
		}
	}
	if (argc > 1) {
		strcpy(url, argv[argc-1]);
	}

	if (strlen(url) == 0) {
		display_usage(argv[0], 1);
	}

	if (min_size < 0 || max_size < 1 || min_size > max_size) {
		fprintf(stderr, "Error: invalid input size constraints\n");
		exit(1);
	}

	if (strlen(directory) == 0) {
		strcpy(directory, getenv("PWD"));
	}

	if (force == 0) {
		if (stat(directory, &st) == -1) {
			fprintf(stderr, "Error: directory doesn't exist\n");
			curl_easy_cleanup(curl_handle);
			curl_global_cleanup();
			exit(1);
		}
		dir_created = 1;
	}

	content.memory = (char *)malloc(CONTENT_MAX_SIZE);
	content.size   = 0;

	curl_global_init(CURL_GLOBAL_ALL);

	curl_handle = curl_easy_init();

	if (verbose == 1)
		printf("Getting html page...\n");

	if ((get_file_remote(curl_handle, url, &content)) != 0) {
		fprintf(stderr, "Error: in getting remote resource %s\n", url);
		free(content.memory);
		curl_easy_cleanup(curl_handle);
		curl_global_cleanup();
		exit(1);
	}

	content.memory[content.size] = 0;
	strcpy(html, content.memory);
	file_size = content.size;
	puts(html);
	if (strlen(extension_list) == 0) {
		strcpy(extension_list, "(\\w+)");
	}

	//printf("%s\n", file_content);

	subject = html;
	subject_length = file_size;
	strcpy(pattern, "<img.+?src\\s*=\\s*['\"]([^>]+)/([^>\\./]+)\\.");
	strcat(pattern, extension_list);
	strcat(pattern, "['\"]");

	re = pcre_compile(pattern, 0, &error, &erroffset, NULL);

	if (re == NULL) {
		fprintf(stderr, "Error: PCRE compilation failed at offset %d: %s\n", erroffset, error);
		free(content.memory);
		curl_easy_cleanup(curl_handle);
		curl_global_cleanup();
		exit(1);
	}

	rc = pcre_exec(re, NULL, subject, subject_length, 0, 0, ovector, OVECCOUNT);
	
	if (rc < 0) {
		switch (rc) {
			case PCRE_ERROR_NOMATCH:
			if (verbose == 1)
				printf("No match\n");
			break;

			default:
			fprintf(stderr, "Error: in executing regular expression matching (%d)\n", rc);
			break;
		}
		free(content.memory);
		curl_easy_cleanup(curl_handle);
		curl_global_cleanup();
		pcre_free(re);
		exit(1);
	}

	link_start       = subject + ovector[2];
    name_start       = subject + ovector[4];
    extension_start  = subject + ovector[6];
    link_length      = ovector[7] - ovector[2];
    name_length      = ovector[7] - ovector[4];
    extension_length = ovector[7] - ovector[6];

	strncpy(img_url, link_start, link_length);
	img_url[link_length] = 0;
	strncpy(img_name, name_start, name_length);
	img_name[name_length] = 0;

	if (verbose == 1)
		printf("Getting image %s...\n", img_url);

	if (get_file_remote(curl_handle, img_url, &content) != 0) {
		fprintf(stderr, "Error: in getting remote resource %s\n", img_url);
		free(content.memory);
		curl_easy_cleanup(curl_handle);
		curl_global_cleanup();
		pcre_free(re);
		exit(1);
	}
	if (min_size <= content.size && content.size <= max_size) {
		if (dir_created == 0) {
			if (do_mkdir(directory, DEFAULT_MODE) == -1) {
				fprintf(stderr, "Error: in creating directory\n");
				free(content.memory);
				curl_easy_cleanup(curl_handle);
				curl_global_cleanup();
				pcre_free(re);
				exit(1);
			}
			dir_created = 1;
		}
		if (save_img(directory, img_name, content) == -1) {
			fprintf(stderr, "Error: in saving image %s\n", img_name);
			free(content.memory);
			curl_easy_cleanup(curl_handle);
			curl_global_cleanup();
			pcre_free(re);
			exit(1);
		}
	}

	while (1) {
		start_offset = ovector[1];
		rc = pcre_exec(re, NULL, subject, subject_length, start_offset, 0, ovector, OVECCOUNT);

		if (rc == PCRE_ERROR_NOMATCH) {
			break;
		}
		else if (rc < 0) {
			fprintf(stderr, "Error: in executing regular expression matching (%d)\n", rc);
			free(content.memory);
			curl_easy_cleanup(curl_handle);
			curl_global_cleanup();
			pcre_free(re);
			exit(1);
		}
		else {
			link_start       = subject + ovector[2];
			name_start       = subject + ovector[4];
			extension_start  = subject + ovector[6];
			link_length      = ovector[7] - ovector[2];
			name_length      = ovector[7] - ovector[4];
			extension_length = ovector[7] - ovector[6];

			strncpy(img_url, link_start, link_length);
			img_url[link_length] = 0;
			strncpy(img_name, name_start, name_length);
			img_name[name_length] = 0;

			if (verbose == 1)
				printf("Getting image %s...\n", img_url);

			if (get_file_remote(curl_handle, img_url, &content) != 0) {
				fprintf(stderr, "Error: in getting remote resource %s\n", img_url);
				free(content.memory);
				curl_easy_cleanup(curl_handle);
				curl_global_cleanup();
				pcre_free(re);
				exit(1);
			}

			if (min_size <= content.size && content.size <= max_size) {
				if (dir_created == 0) {
					if (do_mkdir(directory, DEFAULT_MODE) == -1) {
						fprintf(stderr, "Error: in creating directory\n");
						free(content.memory);
						curl_easy_cleanup(curl_handle);
						curl_global_cleanup();
						pcre_free(re);
						exit(1);
					}
					dir_created = 1;
				}
				if (save_img(directory, img_name, content) == -1) {
					fprintf(stderr, "Error: in saving image %s\n", img_name);
					free(content.memory);
					curl_easy_cleanup(curl_handle);
					curl_global_cleanup();
					pcre_free(re);
					exit(1);
				}
			}
		}
	}

	pcre_free(re);
	free(content.memory);
	curl_easy_cleanup(curl_handle);
	curl_global_cleanup();

	return 0;
}

void usage(const char *program_name, char *usage_string) {
	strcpy(usage_string, "Usage: ");
	strcat(usage_string, program_name);
	strcat(usage_string, " [options] URL\n");
	strcat(usage_string, "Options:\n");
	strcat(usage_string, "\t-d DIR\t\tdirectory to store images, default to current directory\n");
	strcat(usage_string, "\t-f\t\tforce to create all the non-existent directories along the path\n");
	strcat(usage_string, "\t\t\tthis option is only useful if used in conjunction with -d\n");
	strcat(usage_string, "\t-t EXTENSION\tcomma-separated list of image file extensions to save\n");
	strcat(usage_string, "\t-m MIN_SIZE\tminimum size of images to be saved\n");
	strcat(usage_string, "\t-M MAX_SIZE\tmaximum size of images to be saved\n");
	strcat(usage_string, "\t-v\t\tdisplay program's execution information\n");
	strcat(usage_string, "\t-h\t\tdisplay this usage\n");
}

void display_usage(const char *program_name, int invalid) {
	char usage_string[100];
	usage(program_name, usage_string);
	if (invalid) {
		fprintf(stderr, "%s", usage_string);
		exit(1);
	}
	else {
		printf("%s", usage_string);
		exit(0);
	}
}

int read_file(const char *path, char *content) {
	int fd, i = 0;
	fd = open(path, O_RDONLY);
	if (fd == -1)
		return -1;
	while (read(fd, &content[i], 1) == 1) {
		i++;
	}
	content[i] = 0;
	if (close(fd) == -1)
		return -1;
	return i;
}

size_t memory_write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
	size_t real_size = size * nmemb;
	struct memory_holder *mem = (struct memory_holder *)userp;
	memcpy(&(mem->memory[mem->size]), contents, real_size);
	mem->size += real_size;
	return real_size;
}

int get_file_remote(CURL *curl_handle, const char *url, struct memory_holder *memory) {
	memory->size = 0;
	curl_easy_setopt(curl_handle, CURLOPT_URL, url);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, memory_write_callback);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)memory);
	curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");
	return curl_easy_perform(curl_handle);
}

int mkdir_recursive(const char *path, mode_t mode) {
	int   status = 0;
	char *sp, *mp;
	char *copy_path = strdup(path);
	sp = copy_path;
	while (status == 0 && (mp = strchr(sp, '/')) != NULL) {
		if (mp != sp) {
			*mp = '\0';
			if (mkdir(copy_path, mode) == -1 && errno != EEXIST) {
				status = -1;
			}
			*mp = '/';
		}
		sp = mp+1;
	}
	if (status == 0)
		status = mkdir(path, mode);
	free(copy_path);
	return status;
}

int do_mkdir(const char *path, mode_t mode) {
	if (mkdir_recursive(path, mode) == -1 && errno != EEXIST) {
		return -1;
	}
	else {
		return 0;
	}
}

int save_img(const char *directory, const char *name, struct memory_holder content) {
	char *path = (char *)malloc(300);
	int   fd;
	strcpy(path, directory);
	if (path[strlen(path)-1] != '/') {
		strcat(path, "/");
	}
	strcat(path, name);
	fd = open(path, O_WRONLY|O_CREAT, 0755);
	if (fd == -1) {
		free(path);
		return -1;
	}
	write(fd, content.memory, content.size);
	if (close(fd) == -1) {
		free(path);
		return -1;
	}
	free(path);
	return 0;
}
