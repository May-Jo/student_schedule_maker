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
| **Planner** | Build subjects, set retention & proficiency, generate `output.json` |
| **StudyBot** | Groq-powered chat; can update `input.txt` and refresh the schedule |
| **Stopwatch** | Focus sessions with local history |
| **Heatmap** | Visual overview of study vs revision load |

---

## Highlights

- **Confidence-based revision** — Low / Medium / High retention per subject (1, 3, or 5 day gaps)
- **Proficiency-aware scheduling** — Weak subjects get more daily slots; strong subjects are deprioritised
- **Learning profile** — Cramming, Balanced, or Gradual pacing; focus limit before breaks; grasping power
- **Progress & replan** — Check off today’s topics; C engine regenerates remaining days
- **Backward-compatible `input.txt`** — Older 4-field subject lines still work with sensible defaults

---

## Quick start

### Prerequisites

- [GCC](https://www.mingw-w64.org/) (MinGW) on your PATH  
- Python 3.10+  
- PowerShell (Windows)

### 1. Clone & install Python deps

```powershell
git clone https://github.com/May-Jo/student_schedule_maker.git
cd student_schedule_maker
pip install -r requirements.txt
```

### 2. Configure StudyBot (optional)

```powershell
copy .env.example .env
# Edit .env and set GROQ_API_KEY from https://console.groq.com/
```

### 3. Build & run

```powershell
.\build.ps1
```

Open **[http://localhost:8080/](http://localhost:8080/)** in your browser.

> **Tip:** Use the server URL, not `file://` — StudyBot and schedule APIs need the backend.

---

## Project layout

```
student_schedule_maker/
├── src/
│   ├── study.c       # Core scheduler → output.json
│   ├── server.c      # HTTP API + static files
│   └── stopwatch.c   # Standalone stopwatch app
├── public/
│   ├── index.html    # Planner + StudyBot UI
│   ├── stopwatch.html
│   ├── heatmap.html
│   └── css/shared.css
├── bin/              # Built .exe files (after build.ps1)
├── assistant_model.py
├── assistant_api.py
├── input_manager.py
├── input.txt         # Example planner input
├── build.ps1
├── server.ps1
└── requirements.txt
```

---

## `input.txt` format

```text
<subjects> <totalDays>
<name> <topicCount> <difficulty> <examDay> <confidence> <proficiency>
<topic1>
<topic2>
...
<sessionsPerDay>
<studyStyle> <peakHours> <maxSessionsBeforeBreak> <graspingPower>
<missedDays>   # e.g. 0 or 3,5
```

**Example**

```text
2 10
Maths 3 2 7 1 3
Calculus
Algebra
Trigonometry
Physics 2 3 10 3 1
Kinematics
Thermodynamics
2
2 1 2 2
0
```

| Field | Meaning |
|-------|---------|
| **confidence** | 1 Low → revise sooner · 2 Medium · 3 High → longer gap |
| **proficiency** | 1 Weak · 2 Average · 3 Strong (fewer study slots) |
| **studyStyle** | 1 Cramming · 2 Balanced · 3 Gradual |
| **graspingPower** | 1 Slow · 2 Average · 3 Fast |

---

## API (local server)

| Method | Path | Description |
|--------|------|-------------|
| `POST` | `/api/schedule` | Write `input.txt`, run scheduler, return plan |
| `POST` | `/api/replan` | Apply progress from `state.json`, replan |
| `POST` | `/api/chat` | StudyBot message → optional plan update |
| `GET` | `/api/output` | Current `output.json` |
| `GET` | `/api/input` | Current `input.txt` |

---

## Manual compile

```powershell
gcc src\study.c -o bin\study.exe -Wall
gcc src\stopwatch.c -o bin\stopwatch.exe -Wall
gcc src\server.c -o bin\server.exe -lws2_32 -Wall
```

---

## Course context

Built as a **Parallel & Systems Programming** course project: C for the performance-critical scheduler and server, Python for the LLM bridge, and a lightweight web front end.

---

## License

MIT — use and adapt freely for learning and personal projects.

<p align="center">
  <sub>Made with ☕ for students who want a plan, not just a to-do list.</sub>
</p>
