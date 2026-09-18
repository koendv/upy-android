plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "eu.kdvelectronics.upyandroid"
    compileSdk = 37
    ndkVersion = "30.0.16248370"

    defaultConfig {
        applicationId = "eu.kdvelectronics.upyandroid"
        // Bumped 26 -> 27 for android.tf.info()'s hw_nnapi field --
        // libneuralnetworks.so itself needs API 27 (checked
        // ANeuralNetworksModel_create's own __NNAPI_INTRODUCED_IN
        // annotation directly, not assumed). Lets tf_module.cpp link
        // against it directly instead of dlopen/dlsym -- the actual
        // device-enumeration functions still need API 29, still gated
        // by a runtime android_get_device_api_level() check (that part
        // doesn't change with minSdk, see tf_module.cpp's own comment).
        minSdk = 27
        targetSdk = 37
        versionCode = 1
        versionName = "0.1"

        ndk {
            // arm64-v8a only, per project architecture decision.
            abiFilters += "arm64-v8a"
        }

        externalNativeBuild {
            cmake {
                cppFlags += ""
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            // Debug-key-signed, not a production signing identity --
            // this project has no dedicated release key. Simple choice:
            // makes assembleRelease's output genuinely installable
            // (Android refuses to install an unsigned APK) without
            // introducing keystore/secret management. See NOTICE.md/
            // README for the "prototype/datapoint" framing this matches.
            signingConfig = signingConfigs.getByName("debug")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildFeatures {
        aidl = true
        compose = true
    }

    packaging {
        jniLibs {
            // libLiteRtClGlAccelerator.so: a GPU/OpenCL-GL delegate from
            // the litert AAR's own jni/arm64-v8a/ folder -- AGP packages
            // every .so an AAR ships there regardless of whether our
            // code links against it (confirmed directly: it showed up
            // in a real assembled APK although CMakeLists.txt only
            // links libLiteRt.so). android.tf's design scoped
            // acceleration to NNAPI only (best-effort, CPU fallback,
            // see SESSION_STATE.yaml) -- nothing uses a GPU/CL delegate,
            // so this 3.1MB is dead weight, same tier of issue as the
            // litert-api exclusion in this file's dependencies block.
            excludes += "lib/arm64-v8a/libLiteRtClGlAccelerator.so"
        }
    }
}

dependencyLocking {
    lockAllConfigurations()
}

dependencies {
    implementation(platform("androidx.compose:compose-bom:2026.09.00"))
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-tooling-preview")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.activity:activity-compose:1.13.0")
    // File explorer + editor screens (2026-09-16), see SESSION_STATE.yaml.
    // material-icons-core (bundled with material3) only has a small
    // default set -- Folder/Description/UploadFile/CreateNewFolder/
    // Undo/Redo all need the extended pack.
    implementation("androidx.compose.material:material-icons-extended")
    implementation("androidx.navigation:navigation-compose:2.9.8")
    // Nemo Code Editor (MIT, https://github.com/Ma7moud3ly/nemo-editor) --
    // same author as micro-repl, same version they depend on. Verified
    // MIT-licensed before adding (see NOTICE.md).
    implementation("io.github.ma7moud3ly:nemo-editor:1.0.4")
    // LiteRT (TensorFlow Lite's successor, Apache 2.0) -- pulled in for
    // its native libLiteRt.so + C API only (android.tf module, see
    // SESSION_STATE.yaml's "android.tf" design discussion); nothing from
    // its Java/Kotlin API surface is used. NOT com.google.ai.edge.litert:
    // litert-api -- that AAR's own liblitert_jni.so was checked directly
    // (nm -D) and exports zero TfLite* symbols, it's a different-purpose
    // artifact. This is the project's first prebuilt-binary native
    // dependency (see NOTICE.md) -- everything else vendored is compiled
    // from source. The AAR ships no C headers (confirmed empty by
    // extracting it directly) -- headers come from a separate LiteRT
    // source checkout instead, see native-bringup's own LiteRT header
    // vendoring for exactly which files and why.
    //
    // litert-api excluded: a transitive dependency of litert (not
    // declared directly, never used -- confirmed via classes.jar/
    // AndroidManifest.xml inspection it's LiteRT's optional Java "AI
    // Pack" dynamic model-download-from-Play-Store feature, not the
    // inference engine itself). Pulls in real weight for a feature this
    // project has no use for (android.tf loads models from the VFS, same
    // as every other resource here, never from Play): Guava 3.08MB,
    // WorkManager 1.84MB, Play Services basement/tasks, Play Core asset-
    // delivery/ai-delivery, AndroidX Room/SQLite -- ~7MB raw, genuinely
    // shipped in the APK since release's isMinifyEnabled=false strips
    // nothing. Also brings its own FOREGROUND_SERVICE/
    // FOREGROUND_SERVICE_DATA_SYNC manifest permissions ("Required for
    // downloading AiPack models") -- unwanted surface for an offline,
    // script-driven app with no other Play Services dependency anywhere
    // in this project.
    implementation("com.google.ai.edge.litert:litert:2.2.0") {
        exclude(group = "com.google.ai.edge.litert", module = "litert-api")
    }
}
