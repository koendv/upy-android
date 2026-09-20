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
            //
            // If this is ever turned on: litert-api's own proguard.txt
            // only keeps @UsedByReflection-annotated members, NOT
            // JniHandle -- the raw-handle-extraction trick in
            // litert_jni_bridge.cpp (GetFieldID(JniHandle, "handle",
            // "J")) would silently break if R8 renames/removes that
            // field. Not fixed here; inert while this stays false.
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

    // libLiteRtClGlAccelerator.so (a GPU/OpenCL-GL delegate) is staged
    // directly into src/main/jniLibs/arm64-v8a/ by native-bringup/
    // fetch-litert.sh, not sourced from an AAR's own jni/ folder any
    // more (see the litert-api dependency's own comment below for why)
    // -- android.rt's/android.litert's LiteRtEnvironment auto-discovers
    // and dlopen()s it by name at runtime, see rt_module.cpp, so it
    // must actually ship in the APK. AGP scans jniLibs/<abi>/ by
    // convention, no extra packaging config needed. See NOTICE.md for
    // licensing.
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
    // LiteRT's Kotlin/Java API (android.litert module, see
    // litert_module.cpp/LiteRtShim.kt) -- reuses Google's own tested
    // setup/buffer-type-resolution/accelerator-option logic rather than
    // re-deriving it, the way rt_module.cpp had to (and got wrong twice
    // along the way: the dynamic-dim bug, the "options not optional"
    // bug -- see SESSION_STATE.yaml). Real, permanent dependency now --
    // previously added only for a throwaway diagnostic and fully
    // reverted afterward. Accepted APK-size cost (Guava, WorkManager,
    // Play Core classes, ~7MB raw, isMinifyEnabled=false strips
    // nothing) -- verified this project's own use of it (Environment/
    // CompiledModel/TensorBuffer only, never AssetPackManager/
    // ModelProvider) cannot trigger actual Play Store network contact
    // (see SESSION_STATE.yaml's manifest-by-manifest investigation of
    // every transitive dependency). WorkManager's own unconditional
    // auto-init is stripped via AndroidManifest.xml's own provider
    // override, for cleanliness, not because it's unsafe (it's purely
    // local/on-device).
    //
    // NOT also com.google.ai.edge.litert:litert:2.2.0 (the artifact this
    // project used to depend on for android.tf's classic API surface,
    // which this dependency doesn't provide) -- both AARs declare the
    // same namespace ("com.google.ai.edge.litert"), and AGP refuses to
    // merge two libraries sharing one namespace. Resolved by not
    // depending on litert:2.2.0 at all: its only two .so's
    // (libLiteRt.so, libLiteRtClGlAccelerator.so) are already covered
    // by CMakeLists.txt's own `litert` IMPORTED target (auto-packaged
    // by AGP, confirmed present in the built APK) and by
    // fetch-litert.sh's jniLibs staging step, respectively -- neither
    // needs a Gradle dependency to ship. This is still the project's
    // first prebuilt-binary native dependency (see NOTICE.md) --
    // everything else vendored is compiled from source.
    implementation("com.google.ai.edge.litert:litert-api:2.2.0")
}
