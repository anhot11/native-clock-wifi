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
        versionCode = 2
        versionName = "2.0"

        externalNativeBuild {
            cmake {
                cFlags += listOf(
                    "-std=c11",
                    "-Wall",
                    "-O3",
                    "-flto",
                    "-fvisibility=hidden",
                    "-ffunction-sections",
                    "-fdata-sections"
                )
            }
        }
        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a")
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
