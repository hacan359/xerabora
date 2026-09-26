plugins {
    id("com.android.application")
}

// The client's version comes from the upstream header, so a merge that
// bumps it bumps the app. The build number is the commit count: every
// upstream merge raises it, which is what an Android update needs, and a
// local build gets the same number as CI for the same commit.
val repoRoot: File = rootProject.projectDir.parentFile
val upstreamVersion: String = Regex("""#define XERABORA_VERSION "([^"]+)"""")
    .find(File(repoRoot, "client/src/version.h").readText())
    ?.groupValues?.get(1) ?: "0"
val buildNumber: Int = System.getenv("XERABORA_ANDROID_BUILD")?.toInt()
    ?: runCatching {
        providers.exec {
            commandLine("git", "rev-list", "--count", "HEAD")
            workingDir = repoRoot
        }.standardOutput.asText.get().trim().toInt()
    }.getOrDefault(1)
// Where the in-app updater looks for releases: the repository CI runs in.
val updateRepo: String = System.getenv("GITHUB_REPOSITORY") ?: "hacan359/xerabora"

// Release signing, from the environment (CI secrets). Without it the
// release build is left unsigned.
val keystoreFile: String? = System.getenv("XERABORA_KEYSTORE")

android {
    namespace = "io.github.hacan359.xerabora"
    compileSdk = 36
    ndkVersion = "28.2.13676358"

    defaultConfig {
        applicationId = "io.github.hacan359.xerabora"
        minSdk = 26
        targetSdk = 36
        versionCode = buildNumber
        versionName = "$upstreamVersion-android.$buildNumber"
        buildConfigField("String", "UPDATE_REPO", "\"$updateRepo\"")
        buildConfigField("String", "UPSTREAM_VERSION", "\"$upstreamVersion\"")
        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
        }
    }

    buildFeatures {
        buildConfig = true
    }

    signingConfigs {
        if (keystoreFile != null) {
            create("release") {
                storeFile = file(keystoreFile)
                storePassword = System.getenv("XERABORA_KEYSTORE_PASSWORD")
                keyAlias = System.getenv("XERABORA_KEY_ALIAS")
                keyPassword = System.getenv("XERABORA_KEY_PASSWORD")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            if (keystoreFile != null) {
                signingConfig = signingConfigs.getByName("release")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

// The MIT licenses of what the APK carries (the client's covers android/
// too), as assets the About screen shows: MIT asks for the notice to
// travel with every copy. Read from the repository at build time.
abstract class CollectLicenses : DefaultTask() {
    @get:InputFile
    @get:PathSensitive(PathSensitivity.NONE)
    abstract val xerabora: RegularFileProperty

    @get:InputFile
    @get:PathSensitive(PathSensitivity.NONE)
    abstract val rcheevos: RegularFileProperty

    @get:OutputDirectory
    abstract val outputDir: DirectoryProperty

    @TaskAction
    fun collect() {
        val dir = outputDir.get().asFile.resolve("licenses")
        dir.deleteRecursively()
        dir.mkdirs()
        xerabora.get().asFile.copyTo(dir.resolve("xerabora.txt"))
        rcheevos.get().asFile.copyTo(dir.resolve("rcheevos.txt"))
    }
}

val collectLicenses = tasks.register<CollectLicenses>("collectLicenses") {
    xerabora.set(File(repoRoot, "client/LICENSE"))
    rcheevos.set(File(repoRoot, "third_party/rcheevos/LICENSE"))
}

androidComponents {
    onVariants { variant ->
        variant.sources.assets?.addGeneratedSourceDirectory(collectLicenses, CollectLicenses::outputDir)
    }
}
