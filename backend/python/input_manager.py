import os
from dataclasses import dataclass


@dataclass
class Subject:
    name: str
    difficulty: int
    examDay: int
    topics: list


class InputManager:
    def load(self, path="input.txt"):
        if not os.path.exists(path):
            return self._default_data()

        with open(path, "r", encoding="utf-8") as f:
            lines = [line.strip() for line in f.readlines() if line.strip() != ""]

        if not lines:
            return self._default_data()

        header = lines[0].split()
        if len(header) < 2:
            return self._default_data()

        try:
            subject_count = int(header[0])
            total_days = int(header[1])
        except ValueError:
            return self._default_data()

        subjects = []
        index = 1

        for _ in range(subject_count):
            if index >= len(lines):
                break

            parts = lines[index].split()
            if len(parts) < 4:
                raise ValueError("Invalid subject header in input.txt")

            name = parts[0]
            topic_count = int(parts[1])
            difficulty = int(parts[2])
            exam_day = int(parts[3])
            index += 1

            topics = []
            for _ in range(topic_count):
                if index >= len(lines):
                    break
                topics.append(lines[index])
                index += 1

            subjects.append({
                "name": name,
                "difficulty": difficulty,
                "examDay": exam_day,
                "topics": topics,
            })

        sessions_per_day = 1
        missed_days = "0"

        if index < len(lines):
            try:
                sessions_per_day = int(lines[index])
            except ValueError:
                sessions_per_day = 1
            index += 1

        if index < len(lines):
            missed_days = lines[index].strip() or "0"

        return {
            "totalDays": total_days,
            "sessionsPerDay": sessions_per_day,
            "missedDays": missed_days,
            "subjects": subjects,
        }

    def save(self, data, path="input.txt"):
        subjects = data.get("subjects", []) or []
        total_days = max(0, int(data.get("totalDays", 0)))
        sessions_per_day = max(1, int(data.get("sessionsPerDay", 1)))
        missed_days = str(data.get("missedDays", "0")).strip() or "0"

        lines = [f"{len(subjects)} {total_days}"]

        for subject in subjects:
            name = self._normalize_token(subject.get("name", ""))
            topics = [self._normalize_token(str(topic)) for topic in subject.get("topics", []) if str(topic).strip()]
            difficulty = max(1, min(3, int(subject.get("difficulty", 2))))
            exam_day = max(1, int(subject.get("examDay", 1)))

            lines.append(f"{name} {len(topics)} {difficulty} {exam_day}")
            lines.extend(topics)

        lines.append(str(sessions_per_day))
        lines.append(missed_days)

        with open(path, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")

    def add_subject(self, data, name, difficulty, examDay, topics):
        if self._find_subject(data, name) is not None:
            raise ValueError("Subject already exists")

        subject = {
            "name": str(name).strip(),
            "difficulty": max(1, min(3, int(difficulty))),
            "examDay": max(1, int(examDay)),
            "topics": [str(topic).strip() for topic in topics if str(topic).strip()],
        }
        data.setdefault("subjects", []).append(subject)
        return data

    def remove_subject(self, data, name):
        subject = self._find_subject(data, name)
        if subject is None:
            raise ValueError("Subject not found")

        data["subjects"] = [item for item in data.get("subjects", []) if item is not subject]
        return data

    def add_topic(self, data, subjectName, topic):
        subject = self._find_subject(data, subjectName)
        if subject is None:
            raise ValueError("Subject not found")

        normalized = str(topic).strip()
        if not normalized:
            raise ValueError("Topic cannot be empty")

        if any(t.strip().lower() == normalized.lower() for t in subject.get("topics", [])):
            raise ValueError("Topic already exists")

        subject.setdefault("topics", []).append(normalized)
        return data

    def remove_topic(self, data, subjectName, topic):
        subject = self._find_subject(data, subjectName)
        if subject is None:
            raise ValueError("Subject not found")

        normalized = str(topic).strip().lower()
        topics = subject.get("topics", [])
        updated = [t for t in topics if t.strip().lower() != normalized]
        if len(updated) == len(topics):
            raise ValueError("Topic not found")

        subject["topics"] = updated
        return data

    def set_total_days(self, data, days):
        days = max(1, int(days))
        data["totalDays"] = days
        for subject in data.get("subjects", []):
            subject["examDay"] = min(max(1, int(subject.get("examDay", 1))), days)
        return data

    def set_sessions_per_day(self, data, n):
        data["sessionsPerDay"] = max(1, int(n))
        return data

    def set_exam_day(self, data, subjectName, examDay):
        subject = self._find_subject(data, subjectName)
        if subject is None:
            raise ValueError("Subject not found")

        exam_day = max(1, int(examDay))
        total_days = max(1, int(data.get("totalDays", exam_day)))
        subject["examDay"] = min(exam_day, total_days)
        return data

    def set_difficulty(self, data, subjectName, difficulty):
        subject = self._find_subject(data, subjectName)
        if subject is None:
            raise ValueError("Subject not found")

        subject["difficulty"] = max(1, min(3, int(difficulty)))
        return data

    def _find_subject(self, data, name):
        normalized = str(name).strip().lower()
        for subject in data.get("subjects", []):
            if str(subject.get("name", "")).strip().lower() == normalized:
                return subject
        return None

    def _normalize_token(self, value):
        token = str(value).strip()
        if not token:
            return token
        return "_".join(token.split())

    def _default_data(self):
        return {
            "totalDays": 0,
            "sessionsPerDay": 1,
            "missedDays": "0",
            "subjects": [],
        }
