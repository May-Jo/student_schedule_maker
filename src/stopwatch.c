#include <stdio.h>
#include <string.h>
#include <time.h>

#define MAX 50

typedef struct {
    char name[50];
    char date[20];
    char start[20];
    char end[20];
    int duration;
} Session;

Session history[MAX];
int historyCount = 0;

void makeDuration(int totalSeconds, char result[]) {
    int h = totalSeconds / 3600;
    int m = (totalSeconds % 3600) / 60;
    int s = totalSeconds % 60;
    sprintf(result, "%02d:%02d:%02d", h, m, s);
}

void showHistory() {
    if (historyCount == 0) {
        printf("No session history yet.\n");
        return;
    }

    printf("\n----- Session History -----\n");
    for (int i = 0; i < historyCount; i++) {
        char durationText[20];
        makeDuration(history[i].duration, durationText);

        printf("%d. %s\n", i + 1, history[i].name);
        printf("   Date: %s\n", history[i].date);
        printf("   Start: %s | End: %s\n", history[i].start, history[i].end);
        printf("   Duration: %s\n", durationText);
    }
}

void resetSession(int *seconds, int *running, time_t *startTime, time_t *lastRunTime, char name[]) {
    *seconds = 0;
    *running = 0;
    *startTime = 0;
    *lastRunTime = 0;
    strcpy(name, "Study Session");
}

int main() {
    int choice;
    int seconds = 0;
    int running = 0;
    char sessionName[50] = "Study Session";
    time_t startTime = 0;
    time_t lastRunTime = 0;

    while (1) {
        int currentSeconds = seconds;
        char currentDuration[20];

        if (running) {
            currentSeconds += (int)(time(NULL) - lastRunTime);
        }

        makeDuration(currentSeconds, currentDuration);

        printf("\n----- Stopwatch -----\n");
        printf("Session: %s\n", sessionName);
        printf("Time: %s\n", currentDuration);
        printf("1. Rename Current Session\n");
        printf("2. Start\n");
        printf("3. Pause\n");
        printf("4. Reset\n");
        printf("5. Finish and Save Session\n");
        printf("6. Show History\n");
        printf("7. Rename Saved Session\n");
        printf("8. Exit\n");
        printf("Enter choice: ");
        scanf("%d", &choice);
        getchar();

        if (choice == 1) {
            printf("Enter session name: ");
            fgets(sessionName, sizeof(sessionName), stdin);
            sessionName[strcspn(sessionName, "\n")] = '\0';

            if (strlen(sessionName) == 0) {
                strcpy(sessionName, "Study Session");
            }
        } else if (choice == 2) {
            if (!running) {
                running = 1;
                lastRunTime = time(NULL);

                if (startTime == 0) {
                    startTime = lastRunTime;
                }
            }
        } else if (choice == 3) {
            if (running) {
                seconds += (int)(time(NULL) - lastRunTime);
                running = 0;
            }
        } else if (choice == 4) {
            resetSession(&seconds, &running, &startTime, &lastRunTime, sessionName);
        } else if (choice == 5) {
            if (startTime == 0) {
                printf("Start the stopwatch first.\n");
            } else if (historyCount == MAX) {
                printf("History is full.\n");
            } else {
                time_t endTime = time(NULL);

                if (running) {
                    seconds += (int)(endTime - lastRunTime);
                    running = 0;
                }

                struct tm *startInfo = localtime(&startTime);
                strftime(history[historyCount].date, 20, "%d-%m-%Y", startInfo);
                strftime(history[historyCount].start, 20, "%H:%M:%S", startInfo);

                struct tm *endInfo = localtime(&endTime);
                strftime(history[historyCount].end, 20, "%H:%M:%S", endInfo);

                strcpy(history[historyCount].name, sessionName);
                history[historyCount].duration = seconds;
                historyCount++;

                printf("Session saved.\n");
                resetSession(&seconds, &running, &startTime, &lastRunTime, sessionName);
            }
        } else if (choice == 6) {
            showHistory();
        } else if (choice == 7) {
            int number;
            showHistory();

            if (historyCount > 0) {
                printf("Enter session number: ");
                scanf("%d", &number);
                getchar();

                if (number >= 1 && number <= historyCount) {
                    printf("Enter new name: ");
                    fgets(history[number - 1].name, sizeof(history[number - 1].name), stdin);
                    history[number - 1].name[strcspn(history[number - 1].name, "\n")] = '\0';
                } else {
                    printf("Invalid session number.\n");
                }
            }
        } else if (choice == 8) {
            break;
        } else {
            printf("Invalid choice.\n");
        }
    }

    return 0;
}
