pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
        // LazyColumnScrollbar only publishes here, not to Maven Central.
        maven("https://jitpack.io")
    }
}

rootProject.name = "upy-android"
include(":app")
