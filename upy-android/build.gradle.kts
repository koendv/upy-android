plugins {
    // AGP 9.0+ has built-in Kotlin support; the separate
    // org.jetbrains.kotlin.android plugin is no longer needed/allowed.
    id("com.android.application") version "9.4.0" apply false
    // The Compose compiler is still a separate plugin even with AGP's
    // built-in Kotlin support (Kotlin 2.0+ requirement, distinct from
    // the base Kotlin/Android integration).
    id("org.jetbrains.kotlin.plugin.compose") version "2.4.20" apply false
}
