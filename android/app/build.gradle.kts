plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.plugin.compose")
}

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
        versionCode = 16
        versionName = "0.8.2"
        testInstrumentationRunner = "dev.eff.probe.ModStoreChecks"
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
    buildFeatures { compose = true; buildConfig = true }
    externalNativeBuild {
        cmake { path = file("src/main/cpp/CMakeLists.txt"); version = "4.4.4" }
    }
    packaging { jniLibs { useLegacyPackaging = true } }
}

dependencies {
    compileOnly("io.github.libxposed:api:102.0.0")
    implementation(platform("androidx.compose:compose-bom:2026.09.00"))
    implementation("androidx.activity:activity-compose:1.13.0")
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.foundation:foundation")
    implementation("androidx.compose.material:material-icons-extended")
    implementation("androidx.compose.material3:material3:1.5.0-alpha28")
    implementation("top.yukonga.miuix.kmp:miuix-ui-android:0.9.4")
    debugImplementation("androidx.compose.ui:ui-tooling")
}
