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
        minSdk = 26
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
}
