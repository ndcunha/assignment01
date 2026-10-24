#include <stdio.h>
#include <string.h>

#define MAX_LINE 1024
#define MAX_ID 64

/* Extract route id and direction char from a path like
 * simplified_schedules/route_110X_EB.txt  ->  id="110X", dir='E' */
static int parse_filename(const char *path, char *id, size_t id_size, char *dir)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;

    if (strncmp(base, "route_", 6) == 0) {
        base += 6;
    }

    const char *last = strrchr(base, '_');
    if (last == NULL || last == base || last[1] == '\0') {
        return -1;
    }

    size_t len = (size_t)(last - base);
    if (len >= id_size) {
        len = id_size - 1;
    }
    memcpy(id, base, len);
    id[len] = '\0';

    *dir = last[1];
    return 0;
}

/* Read the header line "<int> <route_name>, <int> Stops" and return the stop count. */
static int read_stop_count(FILE *fp)
{
    char line[MAX_LINE];
    int stops = -1;

    if (fgets(line, sizeof line, fp) == NULL) {
        return -1;
    }

    char *comma = strrchr(line, ',');
    if (comma == NULL || sscanf(comma + 1, "%d", &stops) != 1) {
        return -1;
    }
    return stops;
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <list file>\n", argv[0]);
        return 1;
    }

    FILE *list = fopen(argv[1], "r");
    if (list == NULL) {
        perror(argv[1]);
        return 1;
    }

    char path[MAX_LINE];
    int count = 0;

    /* First pass: count non-empty lines */
    while (fgets(path, sizeof path, list) != NULL) {
        path[strcspn(path, "\r\n")] = '\0';
        if (path[0] != '\0') {
            count++;
        }
    }
    rewind(list);

    printf("Processing %d schedule files...\n", count);

    int index = 0;
    while (fgets(path, sizeof path, list) != NULL) {
        path[strcspn(path, "\r\n")] = '\0';
        if (path[0] == '\0') {
            continue;
        }
        index++;

        char id[MAX_ID];
        char dir;
        if (parse_filename(path, id, sizeof id, &dir) != 0) {
            fprintf(stderr, "Could not parse filename: %s\n", path);
            continue;
        }

        FILE *sched = fopen(path, "r");
        if (sched == NULL) {
            perror(path);
            continue;
        }

        int stops = read_stop_count(sched);
        fclose(sched);

        if (stops < 0) {
            fprintf(stderr, "Bad header in: %s\n", path);
            continue;
        }

        printf("schedule #%d is route %s it has %d stops and is in the %c direction\n",
               index, id, stops, dir);
    }

    fclose(list);
    return 0;
}
