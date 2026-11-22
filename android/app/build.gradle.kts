plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "com.nk.nkbuild"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.nk.nkbuild"
        minSdk = 26
        targetSdk = 35
        versionCode = 5
        versionName = "1.3"
    }

    // 固定签名：CI 每次跑的 debug keystore 都是新生成的，签名不一致会导致
    // 新包装不上旧包（INSTALL_FAILED_UPDATE_INCOMPATIBLE），所以这里钉死一把
    signingConfigs {
        create("pinned") {
            storeFile = file("../keystore/nkbuild.p12")
            storeType = "PKCS12"
            storePassword = "nkbuild2026"
            keyAlias = "nkbuild"
            keyPassword = "nkbuild2026"
        }
    }

    buildTypes {
        debug {
            signingConfig = signingConfigs.getByName("pinned")
        }
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("pinned")
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
    buildFeatures {
        compose = true
    }
    packaging {
        resources.excludes += "/META-INF/{AL2.0,LGPL2.1}"
    }
}

dependencies {
    implementation(platform("androidx.compose:compose-bom:2024.12.01"))
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-graphics")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.activity:activity-compose:1.9.3")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.8.7")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.8.7")
    implementation("androidx.documentfile:documentfile:1.0.1")
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.9.0")
    implementation("com.squareup.okhttp3:okhttp:4.12.0")
    // 真·液态玻璃：Haze（API31+ 用 RenderEffect 真模糊背景）
    implementation("dev.chrisbanes.haze:haze:1.5.3")
    implementation("dev.chrisbanes.haze:haze-materials:1.5.3")
}
