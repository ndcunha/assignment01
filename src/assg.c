#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE 1024
#define MAX_ID 64

typedef struct {
    char id[MAX_ID];
    char dir;
    int declared;
    int nstops;
    int *stop_ids;
    char **stop_names;
} Route;

typedef struct {
    int year, month, day, hour, minute;
    long long ts;
    char type;
    int stop;
} Record;

typedef struct {
    Record entry;
    Record exit_rec;
    int exit_stop;
    int guessed;
    int skip;
    int route;
} Trip;

static char *dup_str(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p == NULL) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    memcpy(p, s, n);
    return p;
}

static int cmp_int(const void *a, const void *b)
{
    int x = *(const int *)a;
    int y = *(const int *)b;
    if (x < y) return -1;
    if (x > y) return 1;
    return 0;
}

static int is_leap(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static int days_in_month(int y, int m)
{
    static const int dm[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && is_leap(y)) {
        return 29;
    }
    return dm[m - 1];
}

static long long days_from_civil(int y, int m, int d)
{
    long long days = 0;
    int i;
    
    if (y >= 1970) {
        for (i = 1970; i < y; i++) {
            days += is_leap(i) ? 366 : 365;
        }
    } else {
        for (i = y; i < 1970; i++) {
            days -= is_leap(i) ? 366 : 365;
        }
    }

    for (i = 1; i < m; i++) {
        days += days_in_month(y, i);
    }
    days += (d - 1);
    return days;
}

static int all_digits(const char *s, int from, int to)
{
    for (int i = from; i < to; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return 0;
        }
    }
    return 1;
}

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

static int load_route(const char *path, Route *r)
{
    memset(r, 0, sizeof *r);
    r->declared = -1;

    if (parse_filename(path, r->id, sizeof r->id, &r->dir) != 0) {
        return -1;
    }

    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        perror(path);
        return -1;
    }

    int n = read_stop_count(fp);
    if (n < 0) {
        fclose(fp);
        return -1;
    }
    r->declared = n;
    r->stop_ids = calloc((size_t)n + 1, sizeof(int));
    r->stop_names = calloc((size_t)n + 1, sizeof(char *));
    if (r->stop_ids == NULL || r->stop_names == NULL) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }

    char line[MAX_LINE];
    int got = 0;
    while (got < n && fgets(line, sizeof line, fp) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        char *comma = strchr(line, ',');
        if (comma == NULL) {
            continue;
        }
        *comma = '\0';
        r->stop_ids[got] = atoi(line);
        r->stop_names[got] = dup_str(comma + 1);
        got++;
    }
    r->nstops = got;
    fclose(fp);
    return 0;
}

static int load_all(const char *list_path, Route **out)
{
    FILE *list = fopen(list_path, "r");
    if (list == NULL) {
        perror(list_path);
        return -1;
    }

    char path[MAX_LINE];
    int count = 0;
    while (fgets(path, sizeof path, list) != NULL) {
        path[strcspn(path, "\r\n")] = '\0';
        if (path[0] != '\0') {
            count++;
        }
    }
    rewind(list);

    Route *routes = calloc((size_t)count + 1, sizeof(Route));
    if (routes == NULL) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }

    int i = 0;
    while (fgets(path, sizeof path, list) != NULL) {
        path[strcspn(path, "\r\n")] = '\0';
        if (path[0] == '\0') {
            continue;
        }
        if (load_route(path, &routes[i]) != 0) {
            fprintf(stderr, "Could not load schedule: %s\n", path);
        }
        i++;
    }
    fclose(list);
    *out = routes;
    return count;
}

static int route_index_of_stop(const Route *r, int stop_id)
{
    for (int i = 0; i < r->nstops; i++) {
        if (r->stop_ids[i] == stop_id) {
            return i;
        }
    }
    return -1;
}

static int stop_exists(const Route *routes, int nroutes, int stop_id)
{
    for (int i = 0; i < nroutes; i++) {
        if (route_index_of_stop(&routes[i], stop_id) >= 0) {
            return 1;
        }
    }
    return 0;
}

static int parse_record(const char *line, Record *r)
{
    char buf[MAX_LINE];
    strncpy(buf, line, sizeof buf - 1);
    buf[sizeof buf - 1] = '\0';
    buf[strcspn(buf, "\r\n")] = '\0';

    char *f[4];
    int nf = 0;
    char *p = buf;
    f[nf++] = p;
    while ((p = strchr(p, ',')) != NULL) {
        *p++ = '\0';
        if (nf >= 4) {
            return -1;
        }
        f[nf++] = p;
    }
    if (nf != 4) {
        return -1;
    }

    const char *d = f[0];
    if (strlen(d) != 10 || d[2] != '-' || d[5] != '-' ||
        !all_digits(d, 0, 2) || !all_digits(d, 3, 5) || !all_digits(d, 6, 10)) {
        return -1;
    }
    int mo = atoi(d), da = atoi(d + 3), yr = atoi(d + 6);
    if (mo < 1 || mo > 12 || da < 1 || da > days_in_month(yr, mo)) {
        return -1;
    }

    const char *t = f[1];
    if (strlen(t) != 8 || t[2] != ':' || t[5] != ' ' ||
        !all_digits(t, 0, 2) || !all_digits(t, 3, 5) ||
        (t[6] != 'A' && t[6] != 'P') || t[7] != 'M') {
        return -1;
    }
    int hh = atoi(t), mm = atoi(t + 3);
    if (hh < 1 || hh > 12 || mm < 0 || mm > 59) {
        return -1;
    }
    int h24 = hh % 12 + (t[6] == 'P' ? 12 : 0);

    if (strlen(f[2]) != 1 || (f[2][0] != 'E' && f[2][0] != 'X' && f[2][0] != 'M')) {
        return -1;
    }
    char type = f[2][0];

    const char *loc = f[3];
    size_t ll = strlen(loc);
    int stop = -1;
    if (type == 'M') {
        if (ll > 0 && !all_digits(loc, 0, (int)ll)) {
            return -1;
        }
    } else {
        if (ll == 0 || ll > 9 || !all_digits(loc, 0, (int)ll)) {
            return -1;
        }
        stop = atoi(loc);
    }

    r->year = yr;
    r->month = mo;
    r->day = da;
    r->hour = h24;
    r->minute = mm;
    r->type = type;
    r->stop = stop;
    r->ts = days_from_civil(yr, mo, da) * 86400LL + h24 * 3600LL + mm * 60LL;
    return 0;
}

static int read_records(const char *path, Record **out, int *count)
{
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        return -2;
    }

    char line[MAX_LINE];
    if (fgets(line, sizeof line, fp) == NULL) {
        fclose(fp);
        return -1;
    }
    line[strcspn(line, "\r\n")] = '\0';
    if (line[0] == '\0' || !all_digits(line, 0, (int)strlen(line))) {
        fclose(fp);
        return -1;
    }
    int n = atoi(line);

    Record *recs = calloc((size_t)n + 1, sizeof(Record));
    if (recs == NULL) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }

    for (int i = 0; i < n; i++) {
        if (fgets(line, sizeof line, fp) == NULL || parse_record(line, &recs[i]) != 0) {
            free(recs);
            fclose(fp);
            return -1;
        }
    }
    fclose(fp);
    *out = recs;
    *count = n;
    return 0;
}

static int mode_smallest(int *a, int n)
{
    qsort(a, (size_t)n, sizeof(int), cmp_int);
    int best = a[0], best_count = 0;
    int i = 0;
    while (i < n) {
        int j = i;
        while (j < n && a[j] == a[i]) {
            j++;
        }
        if (j - i > best_count) {
            best_count = j - i;
            best = a[i];
        }
        i = j;
    }
    return best;
}

static int guess_exit(const Trip *trips, int ntrips, int t)
{
    int *cands = malloc(((size_t)ntrips + 1) * sizeof(int));
    if (cands == NULL) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    int nc = 0;

    for (int j = 0; j < t; j++) {
        if (!trips[j].guessed &&
            trips[j].entry.stop == trips[t].entry.stop &&
            trips[j].entry.hour == trips[t].entry.hour) {
            cands[nc++] = trips[j].exit_stop;
        }
    }
    if (nc == 0) {
        for (int j = 0; j < ntrips; j++) {
            if (!trips[j].guessed &&
                trips[j].entry.stop == trips[t].entry.stop &&
                trips[j].entry.hour == trips[t].entry.hour) {
                cands[nc++] = trips[j].exit_stop;
            }
        }
    }

    int result = -1;
    if (nc > 0) {
        result = mode_smallest(cands, nc);
    }
    free(cands);
    return result;
}

static int choose_route(const Route *routes, const int *cand, int k, const Trip *t)
{
    const Route *first = &routes[cand[0]];
    printf("Ambiguous trip detected:\n");
    printf("Entry: %d (%s)\n", t->entry.stop,
           first->stop_names[route_index_of_stop(first, t->entry.stop)]);
    printf("Exit: %d (%s)\n", t->exit_stop,
           first->stop_names[route_index_of_stop(first, t->exit_stop)]);
    printf("Possible routes:\n");
    for (int i = 0; i < k; i++) {
        printf("%d. %s %c\n", i + 1, routes[cand[i]].id, routes[cand[i]].dir);
    }

    char line[MAX_LINE];
    for (;;) {
        printf("Select route (1-%d): \n", k);
        fflush(stdout);
        if (fgets(line, sizeof line, stdin) == NULL) {
            return 0;
        }
        int c;
        if (sscanf(line, "%d", &c) == 1 && c >= 1 && c <= k) {
            return c - 1;
        }
    }
}

static void fmt_iso(const Record *r, char *buf, size_t size)
{
    snprintf(buf, size, "%04d-%02d-%02dT%02d:%02d:00",
             r->year, r->month, r->day, r->hour, r->minute);
}

static int run_final(Route *routes, int nroutes, const char *tap_path)
{
    Record *recs = NULL;
    int nrec = 0;

    int rc = read_records(tap_path, &recs, &nrec);
    if (rc == -2) {
        perror(tap_path);
        return 1;
    }
    if (rc != 0) {
        printf("Invalid data format\n");
        return 0;
    }

    for (int i = 0; i < nrec; i++) {
        if (recs[i].stop >= 0 && !stop_exists(routes, nroutes, recs[i].stop)) {
            printf("Invalid stop ID\n");
            return 0;
        }
    }

    if (nrec % 2 != 0) {
        printf("Invalid data format\n");
        return 0;
    }

    Trip *trips = calloc((size_t)nrec / 2 + 1, sizeof(Trip));
    if (trips == NULL) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    int ntrips = 0;

    for (int i = nrec - 1; i >= 1; i -= 2) {
        Record *e = &recs[i];
        Record *x = &recs[i - 1];
        if (e->type != 'E' || x->type == 'E' || x->ts < e->ts) {
            printf("Invalid data format\n");
            return 0;
        }
        Trip *t = &trips[ntrips++];
        t->entry = *e;
        t->exit_rec = *x;
        t->route = -1;
        if (x->type == 'X') {
            t->exit_stop = x->stop;
        } else {
            t->exit_stop = -1;
            t->guessed = 1;
        }
    }

    for (int i = 1; i < ntrips; i++) {
        Trip key = trips[i];
        int j = i - 1;
        while (j >= 0 && trips[j].entry.ts > key.entry.ts) {
            trips[j + 1] = trips[j];
            j--;
        }
        trips[j + 1] = key;
    }

    for (int t = 0; t < ntrips; t++) {
        if (trips[t].guessed) {
            int g = guess_exit(trips, ntrips, t);
            if (g < 0) {
                printf("Unable to guess exit stop for trip at stop %d\n", trips[t].entry.stop);
                trips[t].skip = 1;
            } else {
                trips[t].exit_stop = g;
            }
        }
    }

    int *cand = malloc(((size_t)nroutes + 1) * sizeof(int));
    if (cand == NULL) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    for (int t = 0; t < ntrips; t++) {
        if (trips[t].skip) {
            continue;
        }
        int k = 0;
        for (int r = 0; r < nroutes; r++) {
            if (route_index_of_stop(&routes[r], trips[t].entry.stop) >= 0 &&
                route_index_of_stop(&routes[r], trips[t].exit_stop) >= 0) {
                cand[k++] = r;
            }
        }
        if (k == 0) {
            printf("No route found for trip %d -> %d\n", trips[t].entry.stop, trips[t].exit_stop);
            trips[t].skip = 1;
        } else if (k == 1) {
            trips[t].route = cand[0];
        } else {
            trips[t].route = cand[choose_route(routes, cand, k, &trips[t])];
        }
    }
    free(cand);

    FILE *out = fopen("output.txt", "w");
    if (out == NULL) {
        perror("output.txt");
        return 1;
    }
    for (int t = 0; t < ntrips; t++) {
        if (trips[t].skip) {
            continue;
        }
        const Route *r = &routes[trips[t].route];
        const char *en = r->stop_names[route_index_of_stop(r, trips[t].entry.stop)];
        const char *ex = r->stop_names[route_index_of_stop(r, trips[t].exit_stop)];
        char ts_in[32], ts_out[32];
        fmt_iso(&trips[t].entry, ts_in, sizeof ts_in);
        fmt_iso(&trips[t].exit_rec, ts_out, sizeof ts_out);
        long long dur = trips[t].exit_rec.ts - trips[t].entry.ts;
        fprintf(out, "%s %c,%s,%s,%s,%s,%lld:%02lld:%02lld%s\n",
                r->id, r->dir, en, ex, ts_in, ts_out,
                dur / 3600, (dur % 3600) / 60, dur % 60,
                trips[t].guessed ? ",Guessed" : "");
    }
    fclose(out);
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc != 2 && argc != 3) {
        fprintf(stderr, "Usage: %s <list file> [tap history file]\n", argv[0]);
        return 1;
    }

    Route *routes = NULL;
    int nroutes = load_all(argv[1], &routes);
    if (nroutes < 0) {
        return 1;
    }

    if (argc == 2) {
        printf("Processing %d schedule files...\n", nroutes);
        for (int i = 0; i < nroutes; i++) {
            if (routes[i].declared < 0) {
                continue;
            }
            printf("schedule #%d is route %s it has %d stops and is in the %c direction\n",
                   i + 1, routes[i].id, routes[i].declared, routes[i].dir);
        }
        return 0;
    }

    return run_final(routes, nroutes, argv[2]);
}