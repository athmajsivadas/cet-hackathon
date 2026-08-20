// ============================================================
//  TEMPLATE — fill in your values from the Firebase Console
//
//  OPTION A (recommended, ~2 minutes):
//    1. Install FlutterFire CLI:  dart pub global activate flutterfire_cli
//    2. Run in this directory:   flutterfire configure
//    3. This file is auto-generated — replace the template below.
//
//  OPTION B (manual):
//    Firebase Console → Project Settings → Your Apps
//    Copy each value from the Web app config and paste below.
//    Repeat for Android (google-services.json can also be used).
// ============================================================

import "package:firebase_core/firebase_core.dart" show FirebaseOptions;
import "package:flutter/foundation.dart"
    show defaultTargetPlatform, kIsWeb, TargetPlatform;

class DefaultFirebaseOptions {
  static FirebaseOptions get currentPlatform {
    if (kIsWeb) return web;
    switch (defaultTargetPlatform) {
      case TargetPlatform.android:
        return android;
      case TargetPlatform.iOS:
        return ios;
      default:
        return web; // fallback for desktop / unknown
    }
  }

  // ── Web (also used for Flutter Web dashboard) ─────────────
  static const FirebaseOptions web = FirebaseOptions(
    apiKey:            "YOUR-WEB-API-KEY",
    authDomain:        "YOUR-PROJECT-ID.firebaseapp.com",
    databaseURL:       "https://YOUR-PROJECT-ID-default-rtdb.firebaseio.com",
    projectId:         "YOUR-PROJECT-ID",
    storageBucket:     "YOUR-PROJECT-ID.appspot.com",
    messagingSenderId: "YOUR-SENDER-ID",
    appId:             "YOUR-WEB-APP-ID",
  );

  // ── Android (mobile trigger app) ──────────────────────────
  static const FirebaseOptions android = FirebaseOptions(
    apiKey:            "YOUR-ANDROID-API-KEY",
    appId:             "YOUR-ANDROID-APP-ID",
    messagingSenderId: "YOUR-SENDER-ID",
    projectId:         "YOUR-PROJECT-ID",
    databaseURL:       "https://YOUR-PROJECT-ID-default-rtdb.firebaseio.com",
    storageBucket:     "YOUR-PROJECT-ID.appspot.com",
  );

  // ── iOS ───────────────────────────────────────────────────
  static const FirebaseOptions ios = FirebaseOptions(
    apiKey:            "YOUR-IOS-API-KEY",
    appId:             "YOUR-IOS-APP-ID",
    messagingSenderId: "YOUR-SENDER-ID",
    projectId:         "YOUR-PROJECT-ID",
    databaseURL:       "https://YOUR-PROJECT-ID-default-rtdb.firebaseio.com",
    storageBucket:     "YOUR-PROJECT-ID.appspot.com",
    iosBundleId:       "com.greencorridor.app",
  );
}
