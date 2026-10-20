plugins { id("com.android.application") }

android {
    namespace = "dev.eff.probe"
    compileSdk = 37
    ndkVersion = "30.0.16248370"
    // Override for another workstation with -PeffNdkPath=...
    providers.gradleProperty("effNdkPath").orNull?.let { ndkPath = it }
    defaultConfig {
        applicationId = "dev.eff.probe"
        minSdk = 29
        targetSdk = 36
        versionCode = 9
        versionName = "0.5.0"
        ndk { abiFilters += "arm64-v8a" }
        externalNativeBuild {
            cmake {
                arguments += "-DANDROID_STL=c++_static"
                targets += "eff_android_probe"
            }
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    externalNativeBuild {
        cmake { path = file("src/main/cpp/CMakeLists.txt"); version = "4.4.4" }
    }
    packaging { jniLibs { useLegacyPackaging = true } }
}

dependencies { compileOnly("io.github.libxposed:api:102.0.0") }
