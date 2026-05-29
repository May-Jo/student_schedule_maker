# Study Planner

<p align="center">
  <strong>Personalised study schedules, progress tracking, and an AI assistant — in one clean web app.</strong>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/C-Scheduling%20Engine-00599C?style=for-the-badge&logo=c" alt="C" />
  <img src="https://img.shields.io/badge/Python-StudyBot-3776AB?style=for-the-badge&logo=python&logoColor=white" alt="Python" />
  <img src="https://img.shields.io/badge/HTML%2FCSS%2FJS-Frontend-E34F26?style=for-the-badge&logo=html5&logoColor=white" alt="Frontend" />
  <img src="https://img.shields.io/badge/Windows-Local%20Server-0078D6?style=for-the-badge&logo=windows" alt="Windows" />
</p>

---

## What it does

**Study Planner** turns your subjects, topics, exam dates, and learning preferences into a day-by-day timetable. Mark what you finished, replan the rest, and ask **StudyBot** to tweak your plan in plain English.

| Module | Role |
|--------|------|
| **Planner** | Build subjects, set retention & proficiency, generate schedule |
| **StudyBot** | Groq-powered chat; can update the plan and refresh the timetable |
| **Stopwatch** | Focus sessions with local history |
| **Heatmap** | Visual overview of study vs revision load |

---

## Project structure

```
student_schedule_maker/
├── backend/
│   ├── c/                    # Scheduling engine & HTTP server (C)
│   │   ├── study.c
│   │   ├── server.c
│   │   └── stopwatch.c
│   └── python/               # StudyBot & input.txt helpers
│       ├── assistant_api.py
│       ├── assistant_model.py
│       ├── input_manager.py
│       └── paths.py
├── frontend/                 # Static web UI
│   ├── index.html
│   ├── stopwatch.html
│   ├── heatmap.html
│   └── css/shared.css
├── data/
│   ├── input.txt             # Active planner input (runtime)
│   ├── examples/
│   │   └── input.sample.txt  # Sample you can copy
│   ├── output.json           # Generated schedule (gitignored)
│   ├── state.json            # Progress for replan (gitignored)
│   └── chat/                 # StudyBot temp files (gitignored)
├── scripts/
│   ├── build.ps1             # Compile + start server
│   └── server.ps1            # PowerShell HTTP server (alternative)
├── bin/                      # Compiled .exe files (after build)
├── build.ps1                 # Wrapper → scripts/build.ps1
├── server.ps1                # Wrapper → scripts/server.ps1
├── requirements.txt
└── .env.example
```

---

## Quick start

### Prerequisites

- [GCC](https://www.mingw-w64.org/) (MinGW) on your PATH  
- Python 3.10+  
- PowerShell (Windows)

### 1. Clone & install

```powershell
git clone https://github.com/May-Jo/student_schedule_maker.git
cd student_schedule_maker
pip install -r requirements.txt
copy .env.example .env
# Optional: set GROQ_API_KEY in .env for StudyBot
```

### 2. Build & run

```powershell
.\build.ps1
```

Open **[http://localhost:8080/](http://localhost:8080/)** (not `file://`).

---

## `data/input.txt` format

```text
<subjects> <totalDays>
<name> <topicCount> <difficulty> <examDay> <confidence> <proficiency>
<topic1>
...
<sessionsPerDay>
<studyStyle> <peakHours> <focusLimit> <graspingPower>
<missedDays>
```

See `data/examples/input.sample.txt` for a full example. Older 4-field subject lines still work (defaults apply).

| Field | Meaning |
|-------|---------|
| **confidence** | 1 Low · 2 Medium · 3 High (revision spacing) |
| **proficiency** | 1 Weak · 2 Average · 3 Strong (time allocation) |
| **studyStyle** | 1 Cramming · 2 Balanced · 3 Gradual |
| **graspingPower** | 1 Slow · 2 Average · 3 Fast |

---

## API (local server)

| Method | Path | Description |
|--------|------|-------------|
| `POST` | `/api/schedule` | Write `data/input.txt`, run scheduler |
| `POST` | `/api/replan` | Apply `data/state.json`, replan |
| `POST` | `/api/chat` | StudyBot |
| `GET` | `/api/output` | Current schedule JSON |
| `GET` | `/api/input` | Current input text |

---

## Manual compile

```powershell
gcc backend\c\study.c -o bin\study.exe -Wall
gcc backend\c\stopwatch.c -o bin\stopwatch.exe -Wall
gcc backend\c\server.c -o bin\server.exe -lws2_32 -Wall
```

Run `bin\server.exe` from the **project root** so `data/` and `frontend/` paths resolve correctly.

---

## License

MIT

<p align="center"><sub>Made for students who want a plan, not just a to-do list.</sub></p>
