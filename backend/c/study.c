/*
 * study.c - Study Planner core scheduler
 * Reads data/input.txt, writes data/output.json.
 *
 * Modes:
 *   study.exe          normal mode, ignores data/state.json
 *   study.exe --replan reads data/state.json and regenerates remaining days
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SUBJECTS 10
#define MAX_TOPICS   50
#define MAX_DAYS     100
#define STATE_BUF    65536

#define DATA_INPUT   "data/input.txt"
#define DATA_OUTPUT  "data/output.json"
#define DATA_STATE   "data/state.json"

typedef struct {
    char name[50];
    int  totalTopics;
    int  difficulty;
    int  examDay;
    int  confidence;   /* 1=Low 2=Medium 3=High */
    int  proficiency;  /* 1=Weak 2=Average 3=Strong */
    char topics[MAX_TOPICS][50];
    int  nextTopic;
} Subject;

Subject subjects[MAX_SUBJECTS];
int subjectCount = 0;
int totalDays = 0;
int missedDays[MAX_DAYS + 2];
int sessionsPerDayInput = 1;
int studyStyle = 2;
int peakHours = 1;
int maxSessionsPerSitting = 2;
int graspingPower = 2;
int studiedDay[MAX_SUBJECTS][MAX_TOPICS];
int completedTopic[MAX_SUBJECTS][MAX_TOPICS];
int topicRating[MAX_SUBJECTS][MAX_TOPICS];
int stateCurrentDay = 0;
int replanMode = 0;

static int imax(int a, int b) { return a > b ? a : b; }

int sessionsPerDay(void) {
    return sessionsPerDayInput;
}

static void json_escape(FILE *out, const char *text) {
    for (const char *p = text; *p; p++) {
        if (*p == '"' || *p == '\\') {
            fputc('\\', out);
            fputc(*p, out);
        } else if (*p == '\n') {
            fputs("\\n", out);
        } else {
            fputc(*p, out);
        }
    }
}

static char *read_whole_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    long len;
    char *buf;

    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    len = ftell(fp);
    rewind(fp);
    if (len < 0 || len >= STATE_BUF) {
        fclose(fp);
        return NULL;
    }

    buf = (char *)calloc((size_t)len + 1, 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }

    fread(buf, 1, (size_t)len, fp);
    fclose(fp);
    return buf;
}

static int find_json_span(const char *start, const char *limit, char open_ch,
                          const char **out_start, const char **out_end) {
    char close_ch = (open_ch == '{') ? '}' : ']';
    const char *p = start;
    int depth = 0;
    int in_string = 0;
    int escape = 0;

    while (p && *p && (!limit || p < limit) && *p != open_ch) p++;
    if (!p || !*p || (limit && p >= limit)) return 0;

    *out_start = p;
    for (; *p && (!limit || p < limit); p++) {
        if (escape) {
            escape = 0;
            continue;
        }
        if (*p == '\\' && in_string) {
            escape = 1;
            continue;
        }
        if (*p == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) continue;

        if (*p == open_ch) depth++;
        if (*p == close_ch) {
            depth--;
            if (depth == 0) {
                *out_end = p + 1;
                return 1;
            }
        }
    }
    return 0;
}

static int find_key_container(const char *section_start, const char *section_end,
                              const char *key, char container,
                              const char **out_start, const char **out_end) {
    char token[128];
    const char *p = section_start;

    snprintf(token, sizeof(token), "\"%s\"", key);
    while ((p = strstr(p, token)) != NULL) {
        const char *colon;
        if (section_end && p >= section_end) break;
        colon = strchr(p + strlen(token), ':');
        if (!colon || (section_end && colon >= section_end)) break;
        if (find_json_span(colon + 1, section_end, container, out_start, out_end)) {
            return 1;
        }
        p += strlen(token);
    }
    return 0;
}

static int key_has_string_value(const char *section_start, const char *section_end,
                                const char *key, const char *value) {
    const char *arr_start;
    const char *arr_end;
    char needle[128];

    if (!find_key_container(section_start, section_end, key, '[', &arr_start, &arr_end)) {
        return 0;
    }

    snprintf(needle, sizeof(needle), "\"%s\"", value);
    return strstr(arr_start, needle) && strstr(arr_start, needle) < arr_end;
}

static int key_int_value(const char *section_start, const char *section_end,
                         const char *key, int *out) {
    char token[128];
    const char *p;
    const char *colon;

    snprintf(token, sizeof(token), "\"%s\"", key);
    p = strstr(section_start, token);
    if (!p || (section_end && p >= section_end)) return 0;

    colon = strchr(p + strlen(token), ':');
    if (!colon || (section_end && colon >= section_end)) return 0;

    *out = atoi(colon + 1);
    return 1;
}

int revisionGap(int confidence, int grasping) {
    int base;

    if (confidence == 1) base = 1;
    else if (confidence == 3) base = 5;
    else base = 3;

    if (grasping == 1) base = imax(1, base - 1);
    if (grasping == 3) base = base + 1;
    return base;
}

int ratingForTopic(int subj, int topic) {
    if (subj < 0 || subj >= subjectCount) return 0;
    if (topic < 0 || topic >= subjects[subj].totalTopics) return 0;
    return topicRating[subj][topic];
}

int calculatePriority(int subjIndex, int currentDay) {
    int daysLeft = subjects[subjIndex].examDay - currentDay;
    int priority = subjects[subjIndex].difficulty;
    int remaining = subjects[subjIndex].totalTopics - subjects[subjIndex].nextTopic;

    priority -= (subjects[subjIndex].proficiency - 1);

    if (daysLeft <= 5) priority += 2;
    if (daysLeft <= 2) priority += 3;
    if (remaining >= 3) priority += 1;
    return priority;
}

static int dailyCap(int subjIndex) {
    if (subjects[subjIndex].proficiency == 3) return 1;
    if (subjects[subjIndex].proficiency == 1) return 3;
    return 2;
}

int shouldRevise(int subj, int topic, int currentDay) {
    int day = studiedDay[subj][topic];
    int gap = revisionGap(subjects[subj].confidence, graspingPower);
    return (day != -1 && (currentDay - day == gap));
}

int isMissedDay(int day) {
    return (day >= 1 && day <= totalDays && missedDays[day] == 1);
}

int countAvailableDays(int startDay) {
    int count = 0;
    for (int day = startDay; day <= totalDays; day++) {
        if (!isMissedDay(day)) count++;
    }
    return (count == 0) ? 1 : count;
}

int remainingTopicCount(void) {
    int remaining = 0;
    for (int i = 0; i < subjectCount; i++) {
        int r = subjects[i].totalTopics - subjects[i].nextTopic;
        if (r > 0) remaining += r;
    }
    return remaining;
}

static void parse_missed_line(const char *line) {
    char buf[256];

    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    {
        char *nl = strchr(buf, '\n');
        if (nl) *nl = '\0';
    }

    if (strcmp(buf, "0") == 0) return;

    {
        char *token = strtok(buf, ",");
        while (token != NULL) {
            int d = atoi(token);
            if (d >= 1 && d <= totalDays) {
                missedDays[d] = 1;
            }
            token = strtok(NULL, ",");
        }
    }
}

static int line_is_preferences(const char *line, int *style, int *peak, int *maxSit, int *grasp) {
    int count = sscanf(line, "%d %d %d %d", style, peak, maxSit, grasp);

    *grasp = 2;
    if (count < 3) return 0;
    if (*style < 1 || *style > 3) return 0;
    if (*peak < 1 || *peak > 2) return 0;
    if (*maxSit < 1 || *maxSit > 5) return 0;
    if (count >= 4) {
        if (*grasp < 1 || *grasp > 3) *grasp = 2;
    }
    return 1;
}

void write_state(void) {
    FILE *out = fopen(DATA_STATE, "w");
    int wroteSubject = 0;

    if (!out) return;

    fprintf(out, "{\n  \"completed\": {\n");
    for (int i = 0; i < subjectCount; i++) {
        int count = 0;
        for (int j = 0; j < subjects[i].totalTopics; j++) {
            if (completedTopic[i][j]) count++;
        }
        if (count == 0) continue;

        if (wroteSubject) fprintf(out, ",\n");
        fprintf(out, "    \"");
        json_escape(out, subjects[i].name);
        fprintf(out, "\": [");

        int wroteTopic = 0;
        for (int j = 0; j < subjects[i].totalTopics; j++) {
            if (!completedTopic[i][j]) continue;
            if (wroteTopic) fprintf(out, ", ");
            fprintf(out, "\"");
            json_escape(out, subjects[i].topics[j]);
            fprintf(out, "\"");
            wroteTopic = 1;
        }
        fprintf(out, "]");
        wroteSubject = 1;
    }
    fprintf(out, "\n  },\n  \"currentDay\": %d,\n  \"ratings\": {\n", stateCurrentDay);

    wroteSubject = 0;
    for (int i = 0; i < subjectCount; i++) {
        int count = 0;
        for (int j = 0; j < subjects[i].totalTopics; j++) {
            if (topicRating[i][j] >= 1 && topicRating[i][j] <= 3) count++;
        }
        if (count == 0) continue;

        if (wroteSubject) fprintf(out, ",\n");
        fprintf(out, "    \"");
        json_escape(out, subjects[i].name);
        fprintf(out, "\": {");

        int wroteTopic = 0;
        for (int j = 0; j < subjects[i].totalTopics; j++) {
            if (topicRating[i][j] < 1 || topicRating[i][j] > 3) continue;
            if (wroteTopic) fprintf(out, ", ");
            fprintf(out, "\"");
            json_escape(out, subjects[i].topics[j]);
            fprintf(out, "\": %d", topicRating[i][j]);
            wroteTopic = 1;
        }
        fprintf(out, "}");
        wroteSubject = 1;
    }
    fprintf(out, "\n  }\n}\n");
    fclose(out);
}

void read_state(void) {
    char *json = read_whole_file(DATA_STATE);
    const char *completed_start = NULL;
    const char *completed_end = NULL;
    const char *ratings_start = NULL;
    const char *ratings_end = NULL;

    if (!json) return;

    key_int_value(json, NULL, "currentDay", &stateCurrentDay);
    if (stateCurrentDay < 0) stateCurrentDay = 0;
    if (stateCurrentDay > totalDays) stateCurrentDay = totalDays;

    find_key_container(json, NULL, "completed", '{', &completed_start, &completed_end);
    find_key_container(json, NULL, "ratings", '{', &ratings_start, &ratings_end);

    for (int i = 0; i < subjectCount; i++) {
        const char *rating_subject_start = NULL;
        const char *rating_subject_end = NULL;

        if (ratings_start && ratings_end) {
            find_key_container(ratings_start, ratings_end, subjects[i].name, '{',
                               &rating_subject_start, &rating_subject_end);
        }

        for (int j = 0; j < subjects[i].totalTopics; j++) {
            if (completed_start && completed_end &&
                key_has_string_value(completed_start, completed_end,
                                     subjects[i].name, subjects[i].topics[j])) {
                completedTopic[i][j] = 1;
            }

            if (rating_subject_start && rating_subject_end) {
                int rating = 0;
                if (key_int_value(rating_subject_start, rating_subject_end,
                                  subjects[i].topics[j], &rating)) {
                    if (rating >= 1 && rating <= 3) topicRating[i][j] = rating;
                }
            }
        }
    }

    free(json);
}

void applyCompletedTopics(void) {
    for (int i = 0; i < subjectCount; i++) {
        int next = 0;
        for (int j = 0; j < subjects[i].totalTopics; j++) {
            if (completedTopic[i][j]) {
                studiedDay[i][j] = stateCurrentDay;
                if (j + 1 > next) next = j + 1;
            }
        }
        if (next > subjects[i].nextTopic) subjects[i].nextTopic = next;
    }
}

void generatePlan(void) {
    FILE *out = fopen(DATA_OUTPUT, "w");
    int startDay = replanMode ? stateCurrentDay + 1 : 1;

    if (!out) {
        fprintf(stderr, "Error: cannot open " DATA_OUTPUT " for writing.\n");
        exit(1);
    }
    if (startDay < 1) startDay = 1;
    if (startDay > totalDays + 1) startDay = totalDays + 1;

    fprintf(out, "{\n");

    for (int day = 1; day <= totalDays; day++) {
        int sessions = sessionsPerDay();
        int usedSessions = 0;
        int wroteItem = 0;
        int topicsStudiedToday[MAX_SUBJECTS];
        int consecutiveStudy = 0;
        int s;

        for (s = 0; s < MAX_SUBJECTS; s++) topicsStudiedToday[s] = 0;

        if (day > 1) fprintf(out, ",\n");
        fprintf(out, "\"day%d\": [", day);

        if (replanMode && day <= stateCurrentDay) {
            fprintf(out, "\"Completed\"");
            fprintf(out, "]");
            continue;
        }

        if (isMissedDay(day)) {
            fprintf(out, "\"Missed day - topics moved ahead\"");
            fprintf(out, "]");
            continue;
        }

        for (int i = 0; i < subjectCount && usedSessions < sessions; i++) {
            for (int j = 0; j < subjects[i].nextTopic && usedSessions < sessions; j++) {
                if (shouldRevise(i, j, day)) {
                    if (wroteItem) fprintf(out, ", ");
                    fprintf(out, "\"Revise: %s - %s\"",
                            subjects[i].name,
                            subjects[i].topics[j]);
                    usedSessions++;
                    wroteItem = 1;
                }
            }
        }

        {
            int remainingTopics = remainingTopicCount();
            int remainingDaysLeft = countAvailableDays(day);
            int studyTarget;

            if (remainingDaysLeft <= 0) remainingDaysLeft = 1;

            if (studyStyle == 1) {
                studyTarget = (remainingTopics + remainingDaysLeft - 1) / remainingDaysLeft + 1;
            } else if (studyStyle == 3) {
                if (day <= totalDays / 2) {
                    studyTarget = remainingTopics / remainingDaysLeft;
                    studyTarget = imax(1, studyTarget - 1);
                } else {
                    studyTarget = (remainingTopics + remainingDaysLeft - 1) / remainingDaysLeft + 1;
                }
            } else {
                studyTarget = (remainingTopics + remainingDaysLeft - 1) / remainingDaysLeft;
            }

            if (graspingPower == 1) studyTarget = imax(1, studyTarget - 1);
            if (graspingPower == 3) studyTarget = studyTarget + 1;

            if (studyTarget > sessions - usedSessions) {
                studyTarget = sessions - usedSessions;
            }

            {
                int studiedToday = 0;

                while (studiedToday < studyTarget && usedSessions < sessions) {
                    int best = -1;
                    int maxPriority = -1;

                    for (int i = 0; i < subjectCount; i++) {
                        if (subjects[i].nextTopic < subjects[i].totalTopics &&
                            topicsStudiedToday[i] < dailyCap(i)) {
                            int p = calculatePriority(i, day);
                            if (p > maxPriority) {
                                maxPriority = p;
                                best = i;
                            }
                        }
                    }
                    if (best == -1) break;

                    {
                        int t = subjects[best].nextTopic;
                        if (wroteItem) fprintf(out, ", ");
                        fprintf(out, "\"Study: %s - %s\"",
                                subjects[best].name,
                                subjects[best].topics[t]);
                        studiedDay[best][t] = day;
                        subjects[best].nextTopic++;
                        topicsStudiedToday[best]++;
                        studiedToday++;
                        usedSessions++;
                        wroteItem = 1;
                        consecutiveStudy++;

                        if (consecutiveStudy == maxSessionsPerSitting &&
                            studiedToday < studyTarget &&
                            usedSessions < sessions) {
                            if (wroteItem) fprintf(out, ", ");
                            fprintf(out, "\"Break\"");
                            usedSessions++;
                            wroteItem = 1;
                            consecutiveStudy = 0;
                        }
                    }
                }
            }
        }

        fprintf(out, "]");
    }

    fprintf(out, "\n}\n");
    fclose(out);
    (void)peakHours;
    printf("Study plan generated successfully -> " DATA_OUTPUT "\n");
}

void readInput(void) {
    FILE *fp = fopen(DATA_INPUT, "r");
    int actualCount = 0;
    char line[512];

    if (!fp) {
        fprintf(stderr, "Error: cannot open " DATA_INPUT ".\n");
        exit(1);
    }

    if (fscanf(fp, "%d %d", &subjectCount, &totalDays) != 2) {
        fprintf(stderr, "Error: malformed " DATA_INPUT " (line 1).\n");
        fclose(fp);
        exit(1);
    }

    if (subjectCount > MAX_SUBJECTS) subjectCount = MAX_SUBJECTS;
    if (totalDays > MAX_DAYS) totalDays = MAX_DAYS;

    fgets(line, sizeof(line), fp);

    for (int i = 0; i < MAX_SUBJECTS; i++) {
        for (int j = 0; j < MAX_TOPICS; j++) {
            studiedDay[i][j] = -1;
            completedTopic[i][j] = 0;
            topicRating[i][j] = 0;
        }
    }

    {
        int expectedSubjects = subjectCount;

        for (int i = 0; i < expectedSubjects; i++) {
        int topicCount = 0;
        int diff = 2;
        int exam = totalDays;
        int conf = 2;
        int prof = 2;
        char name[50] = {0};
        int parsed;

        if (!fgets(line, sizeof(line), fp)) break;

        parsed = sscanf(line, "%49s %d %d %d %d %d", name, &topicCount, &diff, &exam, &conf, &prof);
        if (parsed < 4) break;

        if (conf < 1 || conf > 3) conf = 2;
        if (prof < 1 || prof > 3) prof = 2;
        if (parsed < 5) conf = 2;
        if (parsed < 6) prof = 2;

        strncpy(subjects[actualCount].name, name, 49);
        subjects[actualCount].name[49] = '\0';
        subjects[actualCount].totalTopics = topicCount < MAX_TOPICS ? topicCount : MAX_TOPICS;
        subjects[actualCount].difficulty = diff;
        subjects[actualCount].examDay = exam;
        subjects[actualCount].confidence = conf;
        subjects[actualCount].proficiency = prof;
        if (subjects[actualCount].examDay > totalDays) {
            fprintf(stderr,
                    "Warning: examDay for %s (%d) exceeds totalDays (%d); clamping.\n",
                    subjects[actualCount].name,
                    subjects[actualCount].examDay,
                    totalDays);
            subjects[actualCount].examDay = totalDays;
        }
        subjects[actualCount].nextTopic = 0;

        for (int j = 0; j < subjects[actualCount].totalTopics; j++) {
            if (!fgets(line, sizeof(line), fp)) {
                subjects[actualCount].topics[j][0] = '\0';
                break;
            }
            if (sscanf(line, "%49[^\r\n]", subjects[actualCount].topics[j]) != 1) {
                subjects[actualCount].topics[j][0] = '\0';
            }
        }
        for (int j = subjects[actualCount].totalTopics; j < topicCount; j++) {
            if (!fgets(line, sizeof(line), fp)) break;
        }
        actualCount++;
        }
    }
    subjectCount = actualCount;

    {
        char extra[512];
        if (fgets(extra, sizeof(extra), fp) == NULL) {
            fclose(fp);
            return;
        }
        if (sscanf(extra, "%d", &sessionsPerDayInput) != 1) sessionsPerDayInput = 1;
    }
    if (sessionsPerDayInput < 1) sessionsPerDayInput = 1;

    if (fgets(line, sizeof(line), fp)) {
        int style = 2, peak = 1, maxSit = 2, grasp = 2;

        if (line_is_preferences(line, &style, &peak, &maxSit, &grasp)) {
            studyStyle = style;
            peakHours = peak;
            maxSessionsPerSitting = maxSit;
            graspingPower = grasp;
            if (!fgets(line, sizeof(line), fp)) {
                fclose(fp);
                return;
            }
            parse_missed_line(line);
        } else {
            parse_missed_line(line);
        }
    }

    fclose(fp);
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--replan") == 0) {
        replanMode = 1;
    }

    readInput();
    if (replanMode) {
        read_state();
        applyCompletedTopics();
        write_state();
    }
    generatePlan();
    return 0;
}
