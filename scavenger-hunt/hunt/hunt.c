#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_CURR_PATH_LEN 4096
#define BUFSIZE 200

/*
 * >= 0 : number of bytes read
 * -1   : error
 */
ssize_t ensure_read(int fd, char *buf, size_t size) {
	size_t total = 0;

	while (total < size) {
		ssize_t curr = read(fd, buf + total, size - total);

		if (curr < 0) {
			if (errno == EINTR)
				continue;

			return -1;
		}

		if (curr == 0)
			break;

		total += (size_t)curr;
	}

	return (ssize_t)total;
}

/*
 * 0  : match
 * 1  : not match
 * -1 : error
 */
int is_match(int fd1, int fd2, const struct stat *st1, const struct stat *st2) {
	char buf1[BUFSIZE];
	char buf2[BUFSIZE];

	if (st1->st_dev == st2->st_dev && st1->st_ino == st2->st_ino)
		return 0;

	if (st1->st_size != st2->st_size)
		return 1;

	if (lseek(fd1, 0, SEEK_SET) == (off_t)-1)
		return -1;

	if (lseek(fd2, 0, SEEK_SET) == (off_t)-1)
		return -1;

	while (true) {
		ssize_t curr1 = ensure_read(fd1, buf1, sizeof(buf1));
		ssize_t curr2 = ensure_read(fd2, buf2, sizeof(buf2));

		if (curr1 < 0 || curr2 < 0)
			return -1;

		if (curr1 != curr2)
			return 1;

		if (curr1 == 0)
			return 0;

		if (memcmp(buf1, buf2, (size_t)curr1) != 0)
			return 1;
	}
}

/*
 * 0  : found
 * 1  : not found
 * -1 : error
 */
int tree(const char entry_path[], int trgt_fd, const struct stat *trgt_st) {
	DIR *entry_dir = opendir(entry_path);

	if (!entry_dir) {
		fprintf(stderr, "Can not open %s\n", entry_path);
		return 1;
	}

	struct dirent *dentry;

	while ((dentry = readdir(entry_dir)) != NULL) {
		if (!strcmp(".", dentry->d_name) ||
		    !strcmp("..", dentry->d_name))
			continue;

		char curr_path[MAX_CURR_PATH_LEN + 1];

		int len;

		if (entry_path[strlen(entry_path) - 1] == '/') {
			len = snprintf(curr_path, sizeof(curr_path), "%s%s",
				       entry_path, dentry->d_name);
		} else {
			len = snprintf(curr_path, sizeof(curr_path), "%s/%s",
				       entry_path, dentry->d_name);
		}

		if (len < 0 || (size_t)len >= sizeof(curr_path)) {
			fprintf(stderr,
				"curr_path's buffer too short. Can't open %s "
				"in %s\n",
				dentry->d_name, entry_path);
			continue;
		}

		struct stat curr_st;

		if (lstat(curr_path, &curr_st) == -1) {
			fprintf(stderr, "Can not get stat for %s\n", curr_path);
			continue;
		}

		printf("%30s\t%lu\t%u\n", curr_path,
		       (unsigned long)curr_st.st_ino,
		       (unsigned int)curr_st.st_mode);

		if (S_ISLNK(curr_st.st_mode)) {
			continue;
		} else if (S_ISREG(curr_st.st_mode)) {
			int curr_fd = open(curr_path, O_RDONLY);

			if (curr_fd < 0) {
				fprintf(stderr, "Can not open the file %s\n",
					curr_path);
				continue;
			}

			int ret = is_match(curr_fd, trgt_fd, &curr_st, trgt_st);

			close(curr_fd);

			if (ret == 0) {
				printf("Match path : %s\n", curr_path);
				closedir(entry_dir);
				return 0;
			}

			if (ret < 0) {
				fprintf(stderr, "Can not check is match : %s\n",
					curr_path);
				closedir(entry_dir);
				return -1;
			}
		} else if (S_ISDIR(curr_st.st_mode)) {
			int ret = tree(curr_path, trgt_fd, trgt_st);

			if (ret == 0) {
				closedir(entry_dir);
				return 0;
			}

			if (ret < 0) {
				closedir(entry_dir);
				return -1;
			}
		}
	}

	closedir(entry_dir);

	return 1;
}

int main(int argc, char *argv[]) {
	if (argc != 3) {
		fprintf(stderr, "that ain't how you use this\n");
		return -1;
	}

	int trgt_fd = open(argv[1], O_RDONLY);

	if (trgt_fd < 0) {
		fprintf(stderr, "Can't open target file : %s\n", argv[1]);
		return -1;
	}

	struct stat trgt_st;

	if (fstat(trgt_fd, &trgt_st) == -1) {
		fprintf(stderr, "Can't get stat about target file : %s\n",
			argv[1]);
		close(trgt_fd);
		return -1;
	}

	if (!S_ISREG(trgt_st.st_mode)) {
		fprintf(stderr, "Target must be a regular file : %s\n",
			argv[1]);
		close(trgt_fd);
		return -1;
	}

	int ret = tree(argv[2], trgt_fd, &trgt_st);

	close(trgt_fd);

	if (ret == 0) {
		printf("Found!\n");
	} else if (ret == 1) {
		printf("Not found\n");
	} else if (ret == -1) {
		printf("Error\n");
		return -1;
	}

	return 0;
}