plugins {
    id("com.android.application")
}

android {
    namespace = "com.example.nativec"
    compileSdk = 36

    defaultConfig {
        applicationId = "com.example.nativec"
        minSdk = 30
        targetSdk = 36
        versionCode = 1
        versionName = "1.0"

        externalNativeBuild {
            cmake {
                cFlags += listOf("-std=c11", "-Wall", "-O3")
            }
        }
        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("debug")
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
        }
    }
}
