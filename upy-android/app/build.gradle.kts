import java.io.IOException
import java.time.ZoneOffset
import java.time.ZonedDateTime
import java.time.format.DateTimeFormatter
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

// App version, from version.properties (see there). versionCode is
// derived from it, major*10000 + minor*100 + patch, so every build of
// the same version has the same code and a newer version a higher one.
val appVersion: String = Properties().apply {
    rootProject.file("version.properties").inputStream().use { load(it) }
}.getProperty("version").trim()
val appVersionCode: Int = run {
    val parts = appVersion.split(".").map { it.toIntOrNull() }
    require(parts.size in 2..3 && parts.all { it != null && it in 0..99 }) {
        "version.properties: version must be major.minor[.patch], each 0-99, got '$appVersion'"
    }
    parts[0]!! * 10000 + parts[1]!! * 100 + (parts.getOrNull(2) ?: 0)
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
        versionCode = appVersionCode
        versionName = appVersion

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
    // into src/main/jniLibs/arm64-v8a/ by the stageLitertDelegate task
    // below, from the litert AAR (see the litert-api dependency's own
    // comment below for why that AAR is not a normal dependency).
    // LiteRtEnvironment dlopen()s it by name at runtime, so it must
    // ship in the APK. AGP scans jniLibs/<abi>/ by convention. See
    // NOTICE.html for licensing.
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
        rootProject.file("examples/find_circles/find_circles.py"),
        rootProject.file("examples/find_apriltags/find_apriltags.py"),
        rootProject.file("examples/find_rects/find_rects.py"),
        rootProject.file("examples/find_lines/find_lines.py"),
        rootProject.file("examples/find_edges/find_edges.py"),
        rootProject.file("examples/find_lbp/find_lbp.py"),
        rootProject.file("examples/face_tracking/face_tracking.py"),
        rootProject.file("examples/find_hog/find_hog.py"),
        rootProject.file("examples/find_displacement/find_displacement.py"),
        rootProject.file("examples/find_datamatrices/find_datamatrices.py"),
        rootProject.file("examples/find_template/find_template.py"),
        rootProject.file("examples/location/location.py"),
        rootProject.file("examples/mqtt_tls/mqtt_tls.py"),
        rootProject.file("examples/tflite_selftest/tflite_selftest.py"),
        rootProject.file("examples/litert_selftest/litert_selftest.py"),
        rootProject.file("examples/add_simple/add_simple.tflite"),
        rootProject.file("examples/quant/single_add_default_a8w8_recipe_quantized.tflite"),
    )
    rename("single_add_default_a8w8_recipe_quantized.tflite", "single_add_quant.tflite")
    into(layout.projectDirectory.dir("src/main/assets/examples"))
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
    outputs.dirs(bringup.resolve("vendor/openmv"), bringup.resolve("vendor/ulab"), bringup.resolve("vendor/apriltag"))
    commandLine(bringup.resolve("populate-vendor.sh").path)
}

val genCascades = tasks.register<Exec>("genCascades") {
    dependsOn(fetchUpstream)
    inputs.files(upstreamFile, bringup.resolve("gen-cascades.sh"))
    outputs.dir(bringup.resolve("vendor/rom"))
    commandLine(bringup.resolve("gen-cascades.sh").path)
}

// LiteRT: C headers are committed (src/main/cpp/litert/include/, see the
// README.md there); the two .so files come from the litert AAR. A separate
// configuration, so AGP never merges this AAR (it shares its namespace
// with litert-api).
val litertVersion: String = upstreamProperties.getProperty("litert.version")
val litertHeadersVersion = file("src/main/cpp/litert/include/LITERT_VERSION").readText().trim()
if (litertHeadersVersion != litertVersion) {
    throw GradleException(
        "LiteRT headers are $litertHeadersVersion, upstream.properties says $litertVersion: " +
            "run native-bringup/update-litert-headers.sh, see app/src/main/cpp/litert/README.md",
    )
}

val litertNative: Configuration by configurations.creating {
    isCanBeConsumed = false
    isTransitive = false
}

dependencies {
    litertNative("com.google.ai.edge.litert:litert:$litertVersion@aar")
}

// libLiteRt.so: linked by CMakeLists.txt. libLiteRtClGlAccelerator.so:
// loaded at runtime by name, so it is also staged in jniLibs/ to ship.
val extractLitert = tasks.register<Copy>("extractLitert") {
    from({ zipTree(litertNative.singleFile) }) {
        include("jni/arm64-v8a/*.so")
        eachFile { path = name }
    }
    includeEmptyDirs = false
    into(layout.projectDirectory.dir("src/main/cpp/litert/lib"))
}

// LiteRT's third-party notices, from the AAR, so they match the shipped
// version. Input to genLicenses below.
val litertNoticesDir = layout.buildDirectory.dir("litert-notices")
val extractLitertNotices = tasks.register<Copy>("extractLitertNotices") {
    from({ zipTree(litertNative.singleFile) }) {
        include("THIRD_PARTY_NOTICE.txt")
    }
    into(litertNoticesDir)
}

// Settings > About > Licenses: LICENSES.txt plus LiteRT's notices as one
// HTML page, with LiteRT's ~116 copies of the Apache 2.0 text replaced by
// links to one copy. See native-bringup/gen-licenses.py.
val genLicenses = tasks.register<Exec>("genLicenses") {
    dependsOn(extractLitertNotices)
    val licenses = rootProject.file("LICENSES.txt")
    val notice = litertNoticesDir.map { it.file("THIRD_PARTY_NOTICE.txt") }
    val out = layout.projectDirectory.file("src/main/assets/licenses.html")
    inputs.files(licenses, bringup.resolve("gen-licenses.py"), notice)
    outputs.file(out)
    commandLine("python3", bringup.resolve("gen-licenses.py").path, licenses.path,
        notice.get().asFile.path, out.asFile.path)
    // Replaced by licenses.html; left over in older local checkouts.
    val stale = listOf("LICENSES.txt", "litert_third_party_notices.txt")
        .map { layout.projectDirectory.file("src/main/assets/$it").asFile }
    doFirst { stale.forEach { it.delete() } }
}

val stageLitertDelegate = tasks.register<Copy>("stageLitertDelegate") {
    dependsOn(extractLitert)
    from(layout.projectDirectory.file("src/main/cpp/litert/lib/libLiteRtClGlAccelerator.so"))
    into(layout.projectDirectory.dir("src/main/jniLibs/arm64-v8a"))
}

// The qstr scan also reads this app's own top-level native sources.
val generateEmbed = tasks.register<Exec>("generateEmbed") {
    dependsOn(populateVendor)
    inputs.files(upstreamFile)
    inputs.files(fileTree(bringup.resolve("vendor/openmv")), fileTree(bringup.resolve("vendor/ulab")))
    inputs.files(fileTree(bringup.resolve("vendor/apriltag")))
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

// Build date, source commit and MicroPython version, shown in Settings >
// About. An asset, not BuildConfig, so a new build date does not force a
// Kotlin recompile. Commit: GIT_COMMIT (set by Docker/CI, which have no
// .git) or git rev-parse; "unknown" otherwise. git.dirty: uncommitted
// changes to tracked files (local builds only).
val generateBuildInfo = tasks.register("generateBuildInfo") {
    dependsOn(fetchUpstream)
    val out = layout.projectDirectory.file("src/main/assets/build_info.properties").asFile
    val repoDir = rootProject.projectDir
    val mpconfig = rootProject.file("upstream/micropython/py/mpconfig.h")
    val micropythonSha = upstreamProperties.getProperty("micropython.sha")
    outputs.file(out)
    outputs.upToDateWhen { false }
    doLast {
        val commit = System.getenv("GIT_COMMIT")?.takeIf { it.isNotBlank() } ?: try {
            val p = ProcessBuilder("git", "rev-parse", "HEAD").directory(repoDir).start()
            p.inputStream.bufferedReader().readText().trim().takeIf { p.waitFor() == 0 && it.isNotEmpty() }
        } catch (e: IOException) {
            null
        } ?: "unknown"
        val dirty = System.getenv("GIT_COMMIT").isNullOrBlank() && try {
            val p = ProcessBuilder("git", "status", "--porcelain", "--untracked-files=no").directory(repoDir).start()
            p.inputStream.bufferedReader().readText().isNotBlank().also { p.waitFor() }
        } catch (e: IOException) {
            false
        }
        val defines = mpconfig.readLines().mapNotNull {
            Regex("""#define MICROPY_VERSION_(MAJOR|MINOR|MICRO|PRERELEASE) (\d+)""").find(it)?.destructured
        }.associate { (k, v) -> k to v }
        val mpVersion = "${defines["MAJOR"]}.${defines["MINOR"]}.${defines["MICRO"]}" +
            if (defines["PRERELEASE"] == "1") "-preview" else ""
        val date = ZonedDateTime.now(ZoneOffset.UTC)
            .format(DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm 'UTC'"))
        out.parentFile.mkdirs()
        out.writeText(
            "build.date=$date\n" +
                "git.commit=$commit\n" +
                "git.dirty=$dirty\n" +
                "micropython.version=$mpVersion\n" +
                "micropython.sha=$micropythonSha\n",
        )
    }
}

tasks.named("preBuild") {
    dependsOn(
        copyNotice, copyDemoScripts, copyRom, generateEmbed, extractLitert, stageLitertDelegate,
        genLicenses, generateBuildInfo,
    )
}

// CMake configure/build read micropython_embed/ and litert/lib/ directly.
tasks.matching {
    it.name.startsWith("configureCMake") || it.name.startsWith("buildCMake") ||
        it.name.startsWith("generateJsonModel")
}.configureEach {
    dependsOn(generateEmbed, extractLitert)
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
    implementation("androidx.navigation:navigation-compose:2.9.8")
    // Pinned: these versions used to come in through nemo-editor.
    implementation("androidx.annotation:annotation:1.10.0")
    implementation("androidx.collection:collection:1.6.0")
    implementation("androidx.tracing:tracing:2.0.0")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.11.0")
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
    // reverted afterward. Accepted APK-size cost (Guava;
    // isMinifyEnabled=false strips nothing).
    //
    // NOT also com.google.ai.edge.litert:litert:2.2.0 (the artifact this
    // project used to depend on for android.tf's classic API surface,
    // which this dependency doesn't provide) -- both AARs declare the
    // same namespace ("com.google.ai.edge.litert"), and AGP refuses to
    // merge two libraries sharing one namespace. Resolved by not
    // depending on litert:2.2.0 at all: its only two .so's
    // (libLiteRt.so, libLiteRtClGlAccelerator.so) are already covered
    // by CMakeLists.txt's own `litert` IMPORTED target (auto-packaged
    // by AGP, confirmed present in the built APK) and by the
    // stageLitertDelegate task, respectively. Both are extracted from the
    // litert AAR through the separate litertNative configuration, which
    // AGP never merges. This is still the project's
    // first prebuilt-binary native dependency (see NOTICE.html) --
    // everything else vendored is compiled from source.
    // ai-delivery (Play AI-pack model download, pulls in Play services
    // basement/tasks) is only used by AiPackModelProvider, which we never load.
    implementation("com.google.ai.edge.litert:litert-api:$litertVersion") {
        exclude(group = "com.google.android.play", module = "ai-delivery")
    }
    // umqtt module (umqtt_module.cpp/MqttShim.kt) -- Part 7. Chosen over
    // Eclipse Paho Android: Paho Android has zero tagged GitHub releases
    // (Maven-only publishing), 241 open issues/29 open PRs, and a dual
    // EPL-1.0/EDL-1.0 license (not this project's usual MIT/Apache-2.0).
    // HiveMQ's client is Apache-2.0, actively released, a plain library
    // call with no Service+bound-service ceremony, and its stated minSdk
    // (19+) is already below this project's own (27). See
    // SESSION_STATE.yaml for the full comparison.
    implementation("com.hivemq:hivemq-mqtt-client:1.4.0")
    // camera module (camera_module.cpp/CameraShim.kt).
    implementation("androidx.camera:camera-core:1.6.2")
    implementation("androidx.camera:camera-camera2:1.6.2")
    implementation("androidx.camera:camera-lifecycle:1.6.2")
    // SSH server (Part 8) -- Apache MINA SSHD, the standard actively-
    // maintained JVM SSH server library with real shell-channel support
    // (confirmed choice, see the plan's own Part 8 design). Latest
    // stable GA per Maven Central metadata (3.0.0 is still milestone-
    // only, per this project's own preference for stable releases).
    implementation("org.apache.sshd:sshd-core:2.19.0")
    implementation("org.apache.sshd:sshd-sftp:2.19.0")
}
