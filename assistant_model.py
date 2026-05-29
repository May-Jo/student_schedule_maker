import os
import json
import re
from groq import Groq
from input_manager import InputManager


def build_user_prompt(message, schedule, input_data=None):
    parts = [message]
    if input_data:
        parts.append("\n\nCurrent subjects in input.txt:\n" + json.dumps(input_data.get("subjects", []), indent=2))
    if schedule:
        parts.append("\n\nCurrent generated schedule:\n" + json.dumps(schedule, indent=2))
    return "\n".join(parts)


def _extract_json_object(text):
    if not text:
        return None
    text = text.strip()
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        pass

    fenced = re.search(r"```(?:json)?\s*(\{.*?\})\s*```", text, re.DOTALL | re.IGNORECASE)
    if fenced:
        try:
            return json.loads(fenced.group(1))
        except json.JSONDecodeError:
            pass

    start = text.find("{")
    end = text.rfind("}")
    if start >= 0 and end > start:
        try:
            return json.loads(text[start : end + 1])
        except json.JSONDecodeError:
            pass
    return None


def _apply_actions(manager, data, actions):
    if not actions:
        return False, []

    changed = False
    notes = []

    for action in actions:
        if not isinstance(action, dict):
            continue
        op = str(action.get("op", "")).strip().lower()
        try:
            if op == "add_subject":
                manager.add_subject(
                    data,
                    action.get("name", ""),
                    action.get("difficulty", 2),
                    action.get("examDay", action.get("exam_day", 1)),
                    action.get("topics", []),
                )
                changed = True
            elif op == "remove_subject":
                manager.remove_subject(data, action.get("name", ""))
                changed = True
            elif op == "add_topic":
                manager.add_topic(data, action.get("subject", action.get("name", "")), action.get("topic", ""))
                changed = True
            elif op == "remove_topic":
                manager.remove_topic(data, action.get("subject", action.get("name", "")), action.get("topic", ""))
                changed = True
            elif op == "set_difficulty":
                manager.set_difficulty(data, action.get("subject", action.get("name", "")), action.get("difficulty", 2))
                changed = True
            elif op == "set_exam_day":
                manager.set_exam_day(data, action.get("subject", action.get("name", "")), action.get("examDay", action.get("exam_day", 1)))
                changed = True
            elif op == "set_total_days":
                manager.set_total_days(data, action.get("days", action.get("totalDays", action.get("total_days", 10))))
                changed = True
            elif op == "set_sessions_per_day":
                manager.set_sessions_per_day(data, action.get("sessions", action.get("sessionsPerDay", action.get("sessions_per_day", 1))))
                changed = True
            else:
                notes.append(f"Unknown action ignored: {op}")
        except ValueError as exc:
            notes.append(str(exc))

    return changed, notes


def chat(message, schedule, chat_history, input_path=None):
    api_key = os.environ.get("GROQ_API_KEY", "").strip()
    project_root = os.path.dirname(os.path.abspath(__file__))
    input_path = input_path or os.path.join(project_root, "input.txt")

    if not api_key or api_key == "your_api_key_here":
        return (
            "Hi! It looks like your Groq API key is not configured yet. "
            "Please open the `.env` file in the project directory and replace `your_api_key_here` "
            "with your actual Groq API key (from console.groq.com), then try again."
        )

    manager = InputManager()
    input_data = manager.load(input_path)

    system_prompt = (
        "You are StudyBot, a helpful study-planning assistant.\n"
        "You MUST respond with a single JSON object only (no markdown fences, no extra text).\n\n"
        "Schema:\n"
        "{\n"
        '  "reply": "Short friendly message for the user. Never paste the full schedule or a JSON schedule here.",\n'
        '  "actions": [ ... ]\n'
        "}\n\n"
        "Use actions when the user wants to change their plan (add/remove subjects or topics, change exam day, "
        "difficulty, total days, or sessions per day). Otherwise use an empty actions array.\n\n"
        "Supported actions (field names must match exactly):\n"
        '- {"op":"add_subject","name":"Chemistry","difficulty":2,"examDay":8,"topics":["Stoichiometry","Bonding"]}\n'
        "  difficulty: 1=Easy, 2=Medium, 3=Hard\n"
        '- {"op":"remove_subject","name":"Physics"}\n'
        '- {"op":"add_topic","subject":"Maths","topic":"Integration"}\n'
        '- {"op":"remove_topic","subject":"Maths","topic":"Algebra"}\n'
        '- {"op":"set_difficulty","subject":"Chemistry","difficulty":2}\n'
        '- {"op":"set_exam_day","subject":"Chemistry","examDay":8}\n'
        '- {"op":"set_total_days","days":10}\n'
        '- {"op":"set_sessions_per_day","sessions":2}\n\n'
        f"Current input.txt data:\n{json.dumps(input_data, indent=2)}\n"
    )

    if schedule:
        system_prompt += f"\nCurrent generated schedule (for context only — do not repeat in reply):\n{json.dumps(schedule, indent=2)}\n"
    else:
        system_prompt += "\nNo schedule has been generated yet.\n"

    try:
        client = Groq(api_key=api_key)
        messages = [{"role": "system", "content": system_prompt}]
        for msg in chat_history:
            role = msg.get("role")
            content = msg.get("content")
            if role and content:
                messages.append({"role": role, "content": content})
        messages.append({"role": "user", "content": build_user_prompt(message, schedule, input_data)})

        completion = client.chat.completions.create(
            messages=messages,
            model="openai/gpt-oss-120b",
            temperature=0.3,
            max_tokens=1024,
            response_format={"type": "json_object"},
        )

        raw = completion.choices[0].message.content or ""
        parsed = _extract_json_object(raw)
        if not parsed:
            return raw.strip() or "I could not parse a response. Please try again."

        reply = str(parsed.get("reply", "")).strip()
        actions = parsed.get("actions") or []
        if not isinstance(actions, list):
            actions = []

        plan_changed, notes = _apply_actions(manager, input_data, actions)
        if notes:
            note_text = " ".join(notes)
            reply = f"{reply}\n\nNote: {note_text}".strip() if reply else f"Note: {note_text}"

        if not plan_changed:
            return reply or raw.strip()

        manager.save(input_data, input_path)
        with open(input_path, "r", encoding="utf-8") as f:
            input_text = f.read()

        return {
            "reply": reply or "Your study plan has been updated.",
            "planChanged": True,
            "inputText": input_text,
        }

    except Exception as exc:
        return f"StudyBot encountered an error while communicating with Groq: {exc}"
