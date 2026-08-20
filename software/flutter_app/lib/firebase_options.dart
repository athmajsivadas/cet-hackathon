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

  static const FirebaseOptions web = FirebaseOptions(
    apiKey: 'AIzaSyCy-NB8YJ5_HoUwo6bsx_blWPvJ7X-8zGw',
    appId: '1:493518363980:web:b6cee1010a9c0900b483e9',
    messagingSenderId: '493518363980',
    projectId: 'alt-f4-17fe1',
    authDomain: 'alt-f4-17fe1.firebaseapp.com',
    storageBucket: 'alt-f4-17fe1.firebasestorage.app',
    measurementId: 'G-CY7STFL82D',
    databaseURL: 'https://alt-f4-17fe1-default-rtdb.firebaseio.com',
  );

  // ── Web (also used for Flutter Web dashboard) ─────────────

  static const FirebaseOptions android = FirebaseOptions(
    apiKey: 'AIzaSyCJAuFZLZxHtAD9NXDHY8tqbUfnfo8s3rs',
    appId: '1:493518363980:android:d2f2e378e2da25b3b483e9',
    messagingSenderId: '493518363980',
    projectId: 'alt-f4-17fe1',
    storageBucket: 'alt-f4-17fe1.firebasestorage.app',
    databaseURL: 'https://alt-f4-17fe1-default-rtdb.firebaseio.com',
  );

  // ── Android (mobile trigger app) ──────────────────────────

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