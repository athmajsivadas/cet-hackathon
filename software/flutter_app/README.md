# Green Corridor — Flutter App Setup Guide

## What this app does
- **Dashboard tab**: Shows live node LED status (RED/GREEN/YELLOW) and a
  scrolling event log pulled from Firebase Realtime Database in real time.
- **Trigger tab**: Sends emergency vehicle trigger requests to Firebase
  (secondary/failsafe path — physical button on the ESP32 is always primary).

Works as **Android mobile app** and **Flutter Web** from the same codebase.

---

## Prerequisites
- Flutter SDK ≥ 3.2.0 (run `flutter --version` to check)
- Firebase project with Realtime Database enabled
- The `bridge.py` script running on the laptop (feeds Firebase)

---

## One-time setup (~10 minutes)

### Step 1 — Create the Flutter project scaffold
```bash
# Navigate to the software/ directory
cd "c:\Users\athma\Downloads\CET Hackathon\software"

# Create a new Flutter project (generates android/, ios/, web/, etc.)
flutter create flutter_app --org com.greencorridor --platforms android,web

# The flutter_app/ directory is already created — this just adds the native platform folders.
# When prompted about existing files, choose to keep/overwrite pubspec.yaml with the provided one.
```

### Step 2 — Replace generated lib/ files
The `lib/` folder in this directory contains all the app code.
The `flutter create` command overwrites `lib/main.dart` with a counter app — replace it:
```bash
# (files already in place if you cloned/downloaded the project)
# Just confirm these files exist:
#   lib/main.dart
#   lib/firebase_options.dart
#   lib/services/firebase_service.dart
#   lib/screens/dashboard_screen.dart
#   lib/screens/trigger_screen.dart
```

### Step 3 — Configure Firebase (Option A — FlutterFire CLI, ~3 min)
```bash
# Install FlutterFire CLI (one-time)
dart pub global activate flutterfire_cli

# Inside the flutter_app/ directory:
flutterfire configure

# Select your Firebase project when prompted.
# This auto-generates lib/firebase_options.dart — replaces the template.
```

### Step 4 — Configure Firebase (Option B — Manual, if CLI doesn't work)
1. Firebase Console → your project → Project Settings → Your Apps
2. Click "Add App" → Web (for web) or Android (for mobile)
3. Copy the config values into `lib/firebase_options.dart`

### Step 5 — Set Firebase Realtime Database rules (demo mode)
Firebase Console → Realtime Database → Rules → Paste:
```json
{
  "rules": {
    ".read": true,
    ".write": true
  }
}
```

### Step 6 — Install packages
```bash
cd flutter_app
flutter pub get
```

---

## Running the app

### Android (mobile trigger + dashboard)
```bash
flutter run                    # connects to first available device/emulator
flutter run -d <device-id>     # specific device (see: flutter devices)
```

### Flutter Web (dashboard for judges laptop)
```bash
flutter run -d chrome
# OR build and serve a static bundle:
flutter build web
# Serve the build/web/ folder with any HTTP server
```

---

## Demo checklist (Hour 7–8)

- [ ] `bridge.py` running on laptop with Node 1 connected
- [ ] Firebase shows "bridge_status.online = true"
- [ ] Dashboard shows Node 1, 2, 3 all RED on startup
- [ ] Press Vehicle A trigger → Dashboard shows ALLRED_BUFFER event, then PRECLEAR + Node 1 turns GREEN
- [ ] Flutter mobile trigger button → Firebase `/trigger_requests` gets a new entry (visible in Firebase console)
- [ ] Web dashboard open on judge's view laptop (same Firebase project)

---

## Troubleshooting

| Problem | Fix |
|---|---|
| `firebase_options.dart` compile error | Run `flutterfire configure` or fill in values manually |
| Dashboard stuck on "Waiting for events" | Check bridge.py is running and connected to Firebase |
| Node cards all show RED even when hardware is GREEN | Check bridge.py is online (wifi icon in top-right of app) |
| Trigger button shows "Failed" | Check Firebase rules allow write access |
| Web build fails | Run `flutter build web` and check for dart errors first |
