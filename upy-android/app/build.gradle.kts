import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.plugin.compose")
}

// Pinned third-party versions. Single source of truth, also read by the
// native-bringup/ scripts.
val upstreamProperties = Properties().apply {
    rootProject.file("upstream.properties").inputStream().use { load(it) }
}

android {
    namespace = "eu.kdvelectronics.upyandroid"
    compileSdk = 37
    ndkVersion = "30.0.16248370"

    defaultConfig {
        applicationId = "eu.kdvelectronics.upyandroid"
        // Originally bumped 26 -> 27 for android.tf.info()'s hw_nnapi
        // field (libneuralnetworks.so link), back when that module still
        // existed -- android.tf/android.rt have since been deleted (see
        // git history/SESSION_STATE.yaml), but nothing left in the build
        // needs a lower floor either, so it stays at 27 rather than
        // churning it back down without a real reason to.
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
            // introducing keystore/secret management. See NOTICE.html/
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

    // HiveMQ MQTT Client (umqtt module) pulls in Netty transitively;
    // several of its jars (netty-handler/codec/transport/buffer/
    // resolver/common/transport-native-unix-common) each ship their own
    // copy of these two plain metadata files, which AGP's resource
    // merger refuses to pick one of automatically. Neither is
    // functional at runtime (INDEX.LIST is a JAR-indexing optimization
    // for applet-style classpath scanning; io.netty.versions.properties
    // is a diagnostic version-reporting file) -- safe to drop entirely.
    // META-INF/DEPENDENCIES is the same story again for Apache MINA
    // SSHD (Part 8) -- both sshd-core and sshd-common ship their own
    // copy of this plain, non-functional license-attribution text file.
    packaging {
        resources {
            excludes += "META-INF/INDEX.LIST"
            excludes += "META-INF/io.netty.versions.properties"
            excludes += "META-INF/DEPENDENCIES"
        }
    }

    // libLiteRtClGlAccelerator.so (a GPU/OpenCL-GL delegate) is staged
    // directly into src/main/jniLibs/arm64-v8a/ by native-bringup/
    // fetch-litert.sh, not sourced from an AAR's own jni/ folder any
    // more (see the litert-api dependency's own comment below for why)
    // -- android.rt's/android.litert's LiteRtEnvironment auto-discovers
    // and dlopen()s it by name at runtime, see rt_module.cpp, so it
    // must actually ship in the APK. AGP scans jniLibs/<abi>/ by
    // convention, no extra packaging config needed. See NOTICE.html for
    // licensing.
}

// NOTICE.html is shown in-app (Settings > About) via AboutScreen.kt,
// which reads it as a plain asset -- copied here rather than hand-
// duplicated into src/main/assets/, so there's exactly one copy to keep
// accurate and it can't silently drift out of sync with what actually
// ships.
val copyNotice = tasks.register<Copy>("copyNotice") {
    from(rootProject.file("NOTICE.html"))
    into(layout.projectDirectory.dir("src/main/assets"))
}

// Bundled demo script(s), seeded into the VFS's own /examples/ on first
// launch (or after a version bump) by MainActivity's own
// seedDemoScriptsIfNeeded() -- copied here from the repo's own tracked
// examples/ tree, same "one real copy, not hand-duplicated" reasoning
// as copyNotice above.
val copyDemoScripts = tasks.register<Copy>("copyDemoScripts") {
    from(
        rootProject.file("examples/lcd_shield/lcd_shield.py"),
        rootProject.file("examples/find_line_segments/find_line_segments.py"),
        rootProject.file("examples/face_detection/face_detection.py"),
        rootProject.file("examples/face_eye_detection/face_eye_detection.py"),
        rootProject.file("examples/iris_detection/iris_detection.py"),
    )
    into(layout.projectDirectory.dir("src/main/assets/examples"))
}

// The `ml` library package, seeded into the VFS ROOT (not /examples/ --
// a deliberate, documented exception to that convention) by
// MainActivity's own seedMlLibraryIfNeeded(): it must live at VFS root
// for `import ml` to resolve to it at all (MicroPython's own module
// resolution -- non-extensible builtins, then filesystem, then
// extensible builtins -- only shadows OpenMV's own extensible `ml`/`tf`
// built-in when the filesystem package sits at a location already on
// sys.path, which this port sets to just ['/']). Same "one real copy,
// not hand-duplicated" reasoning as copyNotice/copyDemoScripts above.
val copyMlLibrary = tasks.register<Copy>("copyMlLibrary") {
    from(rootProject.file("libraries/ml/__init__.py"))
    into(layout.projectDirectory.dir("src/main/assets/ml"))
}

// Native inputs, generated from upstream.properties by the
// native-bringup/ scripts. Each task only reruns when its inputs change.
val bringup = rootProject.file("native-bringup")
val upstreamFile = rootProject.file("upstream.properties")

// Cheap no-op once upstream/ is at the pinned commits, so it always runs.
val fetchUpstream = tasks.register<Exec>("fetchUpstream") {
    commandLine(bringup.resolve("fetch-upstream.sh").path)
}

val populateVendor = tasks.register<Exec>("populateVendor") {
    dependsOn(fetchUpstream)
    inputs.files(upstreamFile, bringup.resolve("openmv-manifest.tsv"), bringup.resolve("populate-vendor.sh"))
    outputs.dirs(bringup.resolve("vendor/openmv"), bringup.resolve("vendor/ulab"))
    commandLine(bringup.resolve("populate-vendor.sh").path)
}

val genCascades = tasks.register<Exec>("genCascades") {
    dependsOn(fetchUpstream)
    inputs.files(upstreamFile, bringup.resolve("gen-cascades.sh"))
    outputs.dir(bringup.resolve("vendor/rom"))
    commandLine(bringup.resolve("gen-cascades.sh").path)
}

val fetchLitert = tasks.register<Exec>("fetchLitert") {
    inputs.files(upstreamFile, bringup.resolve("fetch-litert.sh"))
    outputs.dir(layout.projectDirectory.dir("src/main/cpp/litert"))
    outputs.file(layout.projectDirectory.file("src/main/jniLibs/arm64-v8a/libLiteRtClGlAccelerator.so"))
    commandLine(bringup.resolve("fetch-litert.sh").path)
}

// The qstr scan also reads this app's own top-level native sources.
val generateEmbed = tasks.register<Exec>("generateEmbed") {
    dependsOn(populateVendor, fetchLitert)
    inputs.files(upstreamFile)
    inputs.files(fileTree(bringup.resolve("vendor/openmv")), fileTree(bringup.resolve("vendor/ulab")))
    inputs.files(fileTree(bringup.resolve("my-overrides")), fileTree(bringup.resolve("qstr-stub")))
    inputs.files(fileTree(bringup) { include("*.mk", "*.sh", "*.py") })
    inputs.files(fileTree(layout.projectDirectory.dir("src/main/cpp")) { include("*.cpp", "*.h") })
    outputs.dir(layout.projectDirectory.dir("src/main/cpp/micropython_embed"))
    commandLine(bringup.resolve("generate-embed.sh").path)
}

// Haar cascades, seeded into the VFS's /rom/ by MainActivity's
// seedRomIfNeeded().
val copyRom = tasks.register<Copy>("copyRom") {
    dependsOn(genCascades)
    from(bringup.resolve("vendor/rom"))
    into(layout.projectDirectory.dir("src/main/assets/rom"))
}

tasks.named("preBuild") {
    dependsOn(copyNotice, copyDemoScripts, copyMlLibrary, copyRom, generateEmbed, fetchLitert)
}

// CMake configure/build read micropython_embed/ and litert/ directly.
tasks.matching {
    it.name.startsWith("configureCMake") || it.name.startsWith("buildCMake") ||
        it.name.startsWith("generateJsonModel")
}.configureEach {
    dependsOn(generateEmbed, fetchLitert)
}

dependencyLocking {
    lockAllConfigurations()
}

// com.google.android.gms:play-services-basement (pulled in transitively by
// LiteRT's ai-delivery -> Play-Store model download path) declares a
// STRICT constraint pinning androidx.fragment to 1.1.0 -- an explicit
// dependency alone can't outrank a "strictly" constraint, only a force
// can. Without this, lintVitalRelease fails release builds only (debug
// doesn't run lintVital) with InvalidFragmentVersionForActivityResult,
// since MainActivity's registerForActivityResult() needs Fragment >=1.3.0.
configurations.all {
    resolutionStrategy {
        force("androidx.fragment:fragment:1.9.1")
    }
}

dependencies {
    implementation(platform("androidx.compose:compose-bom:2026.09.00"))
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-tooling-preview")
    implementation("androidx.compose.material3:material3")
    // Part 10 -- four peer nav destinations (Command/Files/Camera/
    // Settings), NavigationBar/NavigationRail auto-selected by
    // WindowSizeClass (the Tab A7 hits "expanded" width in landscape,
    // where Material's own guidance prefers a rail over a bottom bar).
    implementation("androidx.compose.material3:material3-adaptive-navigation-suite:1.4.0")
    implementation("androidx.activity:activity-compose:1.13.0")
    // File explorer + editor screens (2026-09-16), see SESSION_STATE.yaml.
    // material-icons-core (bundled with material3) only has a small
    // default set -- Folder/Description/UploadFile/CreateNewFolder/
    // Undo/Redo all need the extended pack.
    implementation("androidx.compose.material:material-icons-extended")
    implementation("androidx.navigation:navigation-compose:2.9.8")
    // Nemo Code Editor (MIT, https://github.com/Ma7moud3ly/nemo-editor) --
    // same author as micro-repl, same version they depend on. Verified
    // MIT-licensed before adding (see NOTICE.html).
    implementation("io.github.ma7moud3ly:nemo-editor:1.0.4")
    // LazyColumnScrollbar (MIT, https://github.com/nanihadesuka/LazyColumnScrollbar) --
    // TerminalScreen's own scrollbar; spike-tested on-device against
    // real mixed-height terminal output before adopting, see
    // session-state. Compose Multiplatform-only as of 3.0.0, so this
    // pulls in a parallel org.jetbrains.compose.* dependency tree
    // alongside this project's own androidx.compose.* (BOM
    // 2026.09.00) -- resolved cleanly in the spike (this project
    // already pins Kotlin 2.4.20, matching this library's own
    // requirement exactly), a real but accepted added-dependency cost.
    implementation("com.github.nanihadesuka.LazyColumnScrollbar:lazycolumnscrollbar:3.0.0")
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
    // first prebuilt-binary native dependency (see NOTICE.html) --
    // everything else vendored is compiled from source.
    implementation("com.google.ai.edge.litert:litert-api:${upstreamProperties.getProperty("litert.version")}")
    // umqtt module (umqtt_module.cpp/MqttShim.kt) -- Part 7. Chosen over
    // Eclipse Paho Android: Paho Android has zero tagged GitHub releases
    // (Maven-only publishing), 241 open issues/29 open PRs, and a dual
    // EPL-1.0/EDL-1.0 license (not this project's usual MIT/Apache-2.0).
    // HiveMQ's client is Apache-2.0, actively released, a plain library
    // call with no Service+bound-service ceremony, and its stated minSdk
    // (19+) is already below this project's own (27). See
    // SESSION_STATE.yaml for the full comparison.
    implementation("com.hivemq:hivemq-mqtt-client:1.4.0")
    // HTTP server (HttpServerManager.kt) -- Part 7's last subitem. CIO
    // engine chosen over Netty/Jetty: pure-Kotlin/coroutines, no extra
    // native/reflection-heavy server framework bundled in, matching this
    // project's own "reuse a known-good, tested platform library"
    // reasoning without pulling in more than this single-connection-at-
    // a-time, same-LAN-only v1 scope actually needs. ktor-server-auth:
    // HTTP Basic auth gate (http_password). ktor-server-status-pages:
    // maps a 404/403 to a real HTTP status instead of a raw exception.
    implementation("io.ktor:ktor-server-core:3.6.0")
    implementation("io.ktor:ktor-server-cio:3.6.0")
    implementation("io.ktor:ktor-server-auth:3.6.0")
    implementation("io.ktor:ktor-server-status-pages:3.6.0")
    // SSH server (Part 8) -- Apache MINA SSHD, the standard actively-
    // maintained JVM SSH server library with real shell-channel support
    // (confirmed choice, see the plan's own Part 8 design). Latest
    // stable GA per Maven Central metadata (3.0.0 is still milestone-
    // only, per this project's own preference for stable releases).
    implementation("org.apache.sshd:sshd-core:2.19.0")
}
