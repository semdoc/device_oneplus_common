/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "android.hardware.biometrics.fingerprint@2.3-service.oneplus"
#define LOG_VERBOSE "android.hardware.biometrics.fingerprint@2.3-service.oneplus"

#include <hardware/hw_auth_token.h>

#include <hardware/hardware.h>
#include <hardware/fingerprint.h>
#include "BiometricsFingerprint.h"

#include <fstream>
#include <inttypes.h>
#include <unistd.h>

#define FINGERPRINT_ACQUIRED_VENDOR 6
#define FINGERPRINT_ERROR_VENDOR 8

#define OP_ENABLE_FP_LONGPRESS 3
#define OP_DISABLE_FP_LONGPRESS 4
#define OP_RESUME_FP_ENROLL 8
#define OP_FINISH_FP_ENROLL 10

#define OP_DISPLAY_AOD_MODE 8
#define OP_DISPLAY_NOTIFY_PRESS 9
#define OP_DISPLAY_SET_DIM 10

#define AUTH_STATUS_PATH  "/sys/class/drm/card0-DSI-1/auth_status"
#define POWER_STATUS_PATH "/sys/class/drm/card0-DSI-1/power_status"
#define DC_DIM_PATH "/sys/class/drm/card0-DSI-1/dimlayer_bl_en"
#define NATIVE_DISPLAY_LOADING_EFFECT "/sys/class/drm/card0-DSI-1/native_display_loading_effect_mode"
#define NATIVE_DISPLAY_CUSTOMER_P3 "/sys/class/drm/card0-DSI-1/native_display_customer_p3_mode"
#define NATIVE_DISPLAY_CUSTOMER_SRGB "/sys/class/drm/card0-DSI-1/native_display_customer_srgb_mode"
#define NATIVE_DISPLAY_P3 "/sys/class/drm/card0-DSI-1/native_display_p3_mode"
#define NATIVE_DISPLAY_SRGB "/sys/class/drm/card0-DSI-1/native_display_srgb_color_mode"
#define NATIVE_DISPLAY_WIDE "/sys/class/drm/card0-DSI-1/native_display_wide_color_mode"


int c_p3,c_srgb,p3,srgb,wide;
bool dcDimState;

/*
 * Write value to path and close file.
 */
template <typename T>
static void set(const std::string& path, const T& value) {
    std::ofstream file(path);
    file << value;
}
template <typename T>
static T get(const std::string& path, const T& def) {
	std::ifstream file(path);
	T result;

	file >> result;
	return file.fail() ? def : result;
}

namespace android {
namespace hardware {
namespace biometrics {
namespace fingerprint {
namespace V2_3 {
namespace implementation {

// Supported fingerprint HAL version
static const uint16_t kVersion = HARDWARE_MODULE_API_VERSION(2, 1);

using RequestStatus =
        android::hardware::biometrics::fingerprint::V2_1::RequestStatus;

BiometricsFingerprint *BiometricsFingerprint::sInstance = nullptr;

BiometricsFingerprint::BiometricsFingerprint() : mClientCallback(nullptr), mDevice(nullptr) {
    sInstance = this; // keep track of the most recent instance
    mDevice = openHal();
    if (!mDevice) {
        ALOGE("Can't open HAL module");
    }

    mVendorFpService = IVendorFingerprintExtensions::getService();
    mVendorDisplayService = IOneplusDisplay::getService();
}

BiometricsFingerprint::~BiometricsFingerprint() {
    ALOGV("~BiometricsFingerprint()");
    if (mDevice == nullptr) {
        ALOGE("No valid device");
        return;
    }
    int err;
    if (0 != (err = mDevice->common.close(
            reinterpret_cast<hw_device_t*>(mDevice)))) {
        ALOGE("Can't close fingerprint module, error: %d", err);
        return;
    }
    mDevice = nullptr;
}

Return<bool> BiometricsFingerprint::isUdfps(uint32_t) {
    return true;
}

Return<void> BiometricsFingerprint::onFingerDown(uint32_t, uint32_t, float, float) {
    mVendorDisplayService->setMode(OP_DISPLAY_SET_DIM, 1); // Fixme! workaround for in-app fod auth
    mVendorDisplayService->setMode(OP_DISPLAY_NOTIFY_PRESS, 1);

    return Void();
}

Return<void> BiometricsFingerprint::onFingerUp() {
    this->isCancelled = 0;
    mVendorDisplayService->setMode(OP_DISPLAY_NOTIFY_PRESS, 0);

    return Void();
}

Return<void> BiometricsFingerprint::onShowUdfpsOverlay() {
    c_p3 = get(NATIVE_DISPLAY_CUSTOMER_P3, 0);
    c_srgb = get(NATIVE_DISPLAY_CUSTOMER_SRGB, 0);
    p3 = get(NATIVE_DISPLAY_P3, 0);
    srgb = get(NATIVE_DISPLAY_SRGB, 0);
    wide = get(NATIVE_DISPLAY_WIDE, 0);
    dcDimState = get(DC_DIM_PATH, 0);
    set(DC_DIM_PATH, 0);
    set(NATIVE_DISPLAY_SRGB, 0);
    set(NATIVE_DISPLAY_P3, 0);
    set(NATIVE_DISPLAY_CUSTOMER_P3, 0);
    set(NATIVE_DISPLAY_CUSTOMER_SRGB, 0);
    set(NATIVE_DISPLAY_LOADING_EFFECT, 1);
    set(NATIVE_DISPLAY_WIDE, 1);

    return Void();
}

Return<void> BiometricsFingerprint::onHideUdfpsOverlay() {
    set(NATIVE_DISPLAY_CUSTOMER_P3, 0);
    set(NATIVE_DISPLAY_CUSTOMER_SRGB, 0);
    set(NATIVE_DISPLAY_P3, 0);
    set(NATIVE_DISPLAY_SRGB, 0);
    set(NATIVE_DISPLAY_LOADING_EFFECT, 0);
    set(NATIVE_DISPLAY_WIDE, 0);
    set(NATIVE_DISPLAY_CUSTOMER_P3, c_p3);
    set(NATIVE_DISPLAY_CUSTOMER_SRGB, c_srgb);
    set(NATIVE_DISPLAY_P3, p3);
    set(NATIVE_DISPLAY_SRGB, srgb);
    set(NATIVE_DISPLAY_WIDE, wide);
    set(DC_DIM_PATH, dcDimState);

    mVendorDisplayService->setMode(OP_DISPLAY_SET_DIM, 0);

    return Void();
}

Return<RequestStatus> BiometricsFingerprint::ErrorFilter(int32_t error) {
    switch(error) {
        case 0: return RequestStatus::SYS_OK;
        case -2: return RequestStatus::SYS_ENOENT;
        case -4: return RequestStatus::SYS_EINTR;
        case -5: return RequestStatus::SYS_EIO;
        case -11: return RequestStatus::SYS_EAGAIN;
        case -12: return RequestStatus::SYS_ENOMEM;
        case -13: return RequestStatus::SYS_EACCES;
        case -14: return RequestStatus::SYS_EFAULT;
        case -16: return RequestStatus::SYS_EBUSY;
        case -22: return RequestStatus::SYS_EINVAL;
        case -28: return RequestStatus::SYS_ENOSPC;
        case -110: return RequestStatus::SYS_ETIMEDOUT;
        default:
            ALOGE("An unknown error returned from fingerprint vendor library: %d", error);
            return RequestStatus::SYS_UNKNOWN;
    }
}

// Translate from errors returned by traditional HAL (see fingerprint.h) to
// HIDL-compliant FingerprintError.
FingerprintError BiometricsFingerprint::VendorErrorFilter(int32_t error,
            int32_t* vendorCode) {
    switch(error) {
        case FINGERPRINT_ERROR_HW_UNAVAILABLE:
            return FingerprintError::ERROR_HW_UNAVAILABLE;
        case FINGERPRINT_ERROR_UNABLE_TO_PROCESS:
            return FingerprintError::ERROR_UNABLE_TO_PROCESS;
        case FINGERPRINT_ERROR_TIMEOUT:
            return FingerprintError::ERROR_TIMEOUT;
        case FINGERPRINT_ERROR_NO_SPACE:
            return FingerprintError::ERROR_NO_SPACE;
        case FINGERPRINT_ERROR_CANCELED:
            return FingerprintError::ERROR_CANCELED;
        case FINGERPRINT_ERROR_UNABLE_TO_REMOVE:
            return FingerprintError::ERROR_UNABLE_TO_REMOVE;
        case FINGERPRINT_ERROR_LOCKOUT:
            return FingerprintError::ERROR_LOCKOUT;
        default:
            if (error >= FINGERPRINT_ERROR_VENDOR_BASE) {
                // vendor specific code.
                *vendorCode = error - FINGERPRINT_ERROR_VENDOR_BASE;
                return FingerprintError::ERROR_VENDOR;
            }
    }
    ALOGE("Unknown error from fingerprint vendor library: %d", error);
    return FingerprintError::ERROR_UNABLE_TO_PROCESS;
}

// Translate acquired messages returned by traditional HAL (see fingerprint.h)
// to HIDL-compliant FingerprintAcquiredInfo.
FingerprintAcquiredInfo BiometricsFingerprint::VendorAcquiredFilter(
        int32_t info, int32_t* vendorCode) {
    switch(info) {
        case FINGERPRINT_ACQUIRED_GOOD:
            return FingerprintAcquiredInfo::ACQUIRED_GOOD;
        case FINGERPRINT_ACQUIRED_PARTIAL:
            return FingerprintAcquiredInfo::ACQUIRED_PARTIAL;
        case FINGERPRINT_ACQUIRED_INSUFFICIENT:
            return FingerprintAcquiredInfo::ACQUIRED_INSUFFICIENT;
        case FINGERPRINT_ACQUIRED_IMAGER_DIRTY:
            return FingerprintAcquiredInfo::ACQUIRED_IMAGER_DIRTY;
        case FINGERPRINT_ACQUIRED_TOO_SLOW:
            return FingerprintAcquiredInfo::ACQUIRED_TOO_SLOW;
        case FINGERPRINT_ACQUIRED_TOO_FAST:
            return FingerprintAcquiredInfo::ACQUIRED_TOO_FAST;
        default:
            if (info >= FINGERPRINT_ACQUIRED_VENDOR_BASE) {
                // vendor specific code.
                *vendorCode = info - FINGERPRINT_ACQUIRED_VENDOR_BASE;
                return FingerprintAcquiredInfo::ACQUIRED_VENDOR;
            }
    }
    ALOGE("Unknown acquiredmsg from fingerprint vendor library: %d", info);
    return FingerprintAcquiredInfo::ACQUIRED_INSUFFICIENT;
}

Return<uint64_t> BiometricsFingerprint::setNotify(
        const sp<IBiometricsFingerprintClientCallback>& clientCallback) {
    std::lock_guard<std::mutex> lock(mClientCallbackMutex);
    mClientCallback = clientCallback;
    // This is here because HAL 2.3 doesn't have a way to propagate a
    // unique token for its driver. Subsequent versions should send a unique
    // token for each call to setNotify(). This is fine as long as there's only
    // one fingerprint device on the platform.
    return reinterpret_cast<uint64_t>(mDevice);
}

Return<uint64_t> BiometricsFingerprint::preEnroll()  {
    set(POWER_STATUS_PATH, 1);

    return mDevice->pre_enroll(mDevice);
}

Return<RequestStatus> BiometricsFingerprint::enroll(const hidl_array<uint8_t, 69>& hat,
        uint32_t gid, uint32_t timeoutSec) {
    mVendorDisplayService->setMode(OP_DISPLAY_SET_DIM, 1);
    mVendorFpService->updateStatus(OP_DISABLE_FP_LONGPRESS);
    mVendorFpService->updateStatus(OP_RESUME_FP_ENROLL);

    const hw_auth_token_t* authToken =
        reinterpret_cast<const hw_auth_token_t*>(hat.data());
    return ErrorFilter(mDevice->enroll(mDevice, authToken, gid, timeoutSec));
}

Return<RequestStatus> BiometricsFingerprint::postEnroll() {
    mVendorDisplayService->setMode(OP_DISPLAY_SET_DIM, 0);
    mVendorFpService->updateStatus(OP_FINISH_FP_ENROLL);
    mVendorFpService->updateStatus(OP_ENABLE_FP_LONGPRESS);

    return ErrorFilter(mDevice->post_enroll(mDevice));
}

Return<uint64_t> BiometricsFingerprint::getAuthenticatorId() {
    return mDevice->get_authenticator_id(mDevice);
}

Return<RequestStatus> BiometricsFingerprint::cancel() {
    this->isCancelled = 1;
    mVendorDisplayService->setMode(OP_DISPLAY_SET_DIM, 0);
    mVendorFpService->updateStatus(OP_FINISH_FP_ENROLL);
    mVendorFpService->updateStatus(OP_ENABLE_FP_LONGPRESS);
    set(AUTH_STATUS_PATH, 2);

    return ErrorFilter(mDevice->cancel(mDevice));
}

Return<RequestStatus> BiometricsFingerprint::enumerate()  {
    return ErrorFilter(mDevice->enumerate(mDevice));
}

Return<RequestStatus> BiometricsFingerprint::remove(uint32_t gid, uint32_t fid) {
    return ErrorFilter(mDevice->remove(mDevice, gid, fid));
}

Return<RequestStatus> BiometricsFingerprint::setActiveGroup(uint32_t gid,
        const hidl_string& storePath) {
    if (storePath.size() >= PATH_MAX || storePath.size() <= 0) {
        ALOGE("Bad path length: %zd", storePath.size());
        return RequestStatus::SYS_EINVAL;
    }
    if (access(storePath.c_str(), W_OK)) {
        return RequestStatus::SYS_EINVAL;
    }

    return ErrorFilter(mDevice->set_active_group(mDevice, gid,
                                                    storePath.c_str()));
}

Return<RequestStatus> BiometricsFingerprint::authenticate(uint64_t operationId,
        uint32_t gid) {
    set(POWER_STATUS_PATH, 1);
    if (this->isCancelled) {
        mVendorDisplayService->setMode(OP_DISPLAY_SET_DIM, 1);
        set(AUTH_STATUS_PATH, 0);
    } else {
        set(AUTH_STATUS_PATH, 1);
    }
    mVendorFpService->updateStatus(OP_ENABLE_FP_LONGPRESS);

    return ErrorFilter(mDevice->authenticate(mDevice, operationId, gid));
}

IBiometricsFingerprint* BiometricsFingerprint::getInstance() {
    if (!sInstance) {
      sInstance = new BiometricsFingerprint();
    }
    return sInstance;
}

const char* BiometricsFingerprint::getModuleId() {
    int sensor_version = -1;
    std::ifstream file("/sys/devices/platform/soc/soc:fingerprint_detect/sensor_version");
    file >> sensor_version;
    ALOGI("fp sensor version is: 0x%x", sensor_version);
    return sensor_version == 0x9638 ? "goodix.g6.fod" : "goodix.fod";
}

fingerprint_device_t* BiometricsFingerprint::openHal() {
    int err;
    const hw_module_t *hw_mdl = nullptr;
    ALOGD("Opening fingerprint hal library...");
    if (0 != (err = hw_get_module(getModuleId(), &hw_mdl))) {
        ALOGE("Can't open fingerprint HW Module, error: %d", err);
        return nullptr;
    }

    if (hw_mdl == nullptr) {
        ALOGE("No valid fingerprint module");
        return nullptr;
    }

    fingerprint_module_t const *module =
        reinterpret_cast<const fingerprint_module_t*>(hw_mdl);
    if (module->common.methods->open == nullptr) {
        ALOGE("No valid open method");
        return nullptr;
    }

    hw_device_t *device = nullptr;

    if (0 != (err = module->common.methods->open(hw_mdl, nullptr, &device))) {
        ALOGE("Can't open fingerprint methods, error: %d", err);
        return nullptr;
    }

    if (kVersion != device->version) {
        // enforce version on new devices because of HIDL@2.3 translation layer
        ALOGE("Wrong fp version. Expected %d, got %d", kVersion, device->version);
        return nullptr;
    }

    fingerprint_device_t* fp_device =
        reinterpret_cast<fingerprint_device_t*>(device);

    if (0 != (err =
            fp_device->set_notify(fp_device, BiometricsFingerprint::notify))) {
        ALOGE("Can't register fingerprint module callback, error: %d", err);
        return nullptr;
    }

    return fp_device;
}

void BiometricsFingerprint::notify(const fingerprint_msg_t *msg) {
    BiometricsFingerprint* thisPtr = static_cast<BiometricsFingerprint*>(
            BiometricsFingerprint::getInstance());
    std::lock_guard<std::mutex> lock(thisPtr->mClientCallbackMutex);
    if (thisPtr == nullptr || thisPtr->mClientCallback == nullptr) {
        ALOGE("Receiving callbacks before the client callback is registered.");
        return;
    }
    const uint64_t devId = reinterpret_cast<uint64_t>(thisPtr->mDevice);
    switch (msg->type) {
        case FINGERPRINT_ERROR: {
                int32_t vendorCode = 0;
                FingerprintError result = VendorErrorFilter(msg->data.error, &vendorCode);
                ALOGD("onError(%d)", result);
                if (!thisPtr->mClientCallback->onError(devId, result, vendorCode).isOk()) {
                    ALOGE("failed to invoke fingerprint onError callback");
                }
            }
            break;
        case FINGERPRINT_ACQUIRED: {
                int32_t vendorCode = 0;
                FingerprintAcquiredInfo result =
                    VendorAcquiredFilter(msg->data.acquired.acquired_info, &vendorCode);
                ALOGD("onAcquired(%d)", result);
                if (!thisPtr->mClientCallback->onAcquired(devId, result, vendorCode).isOk()) {
                    ALOGE("failed to invoke fingerprint onAcquired callback");
                }
            }
            break;
        case FINGERPRINT_TEMPLATE_ENROLLING:
            ALOGD("onEnrollResult(fid=%d, gid=%d, rem=%d)",
                msg->data.enroll.finger.fid,
                msg->data.enroll.finger.gid,
                msg->data.enroll.samples_remaining);
            if (!thisPtr->mClientCallback->onEnrollResult(devId,
                    msg->data.enroll.finger.fid,
                    msg->data.enroll.finger.gid,
                    msg->data.enroll.samples_remaining).isOk()) {
                ALOGE("failed to invoke fingerprint onEnrollResult callback");
            }
            break;
        case FINGERPRINT_TEMPLATE_REMOVED:
            ALOGD("onRemove(fid=%d, gid=%d, rem=%d)",
                msg->data.removed.finger.fid,
                msg->data.removed.finger.gid,
                msg->data.removed.remaining_templates);
            if (!thisPtr->mClientCallback->onRemoved(devId,
                    msg->data.removed.finger.fid,
                    msg->data.removed.finger.gid,
                    msg->data.removed.remaining_templates).isOk()) {
                ALOGE("failed to invoke fingerprint onRemoved callback");
            }
            break;
        case FINGERPRINT_AUTHENTICATED:
            if (msg->data.authenticated.finger.fid != 0) {
                ALOGD("onAuthenticated(fid=%d, gid=%d)",
                    msg->data.authenticated.finger.fid,
                    msg->data.authenticated.finger.gid);
                const uint8_t* hat =
                    reinterpret_cast<const uint8_t *>(&msg->data.authenticated.hat);
                const hidl_vec<uint8_t> token(
                    std::vector<uint8_t>(hat, hat + sizeof(msg->data.authenticated.hat)));
                if (!thisPtr->mClientCallback->onAuthenticated(devId,
                        msg->data.authenticated.finger.fid,
                        msg->data.authenticated.finger.gid,
                        token).isOk()) {
                    ALOGE("failed to invoke fingerprint onAuthenticated callback");
                }
                getInstance()->onFingerUp();
            } else {
                // Not a recognized fingerprint
                if (!thisPtr->mClientCallback->onAuthenticated(devId,
                        msg->data.authenticated.finger.fid,
                        msg->data.authenticated.finger.gid,
                        hidl_vec<uint8_t>()).isOk()) {
                    ALOGE("failed to invoke fingerprint onAuthenticated callback");
                }
            }
            break;
        case FINGERPRINT_TEMPLATE_ENUMERATING:
            ALOGD("onEnumerate(fid=%d, gid=%d, rem=%d)",
                msg->data.enumerated.finger.fid,
                msg->data.enumerated.finger.gid,
                msg->data.enumerated.remaining_templates);
            if (!thisPtr->mClientCallback->onEnumerate(devId,
                    msg->data.enumerated.finger.fid,
                    msg->data.enumerated.finger.gid,
                    msg->data.enumerated.remaining_templates).isOk()) {
                ALOGE("failed to invoke fingerprint onEnumerate callback");
            }
            break;
    }
}

} // namespace implementation
}  // namespace V2_3
}  // namespace fingerprint
}  // namespace biometrics
}  // namespace hardware
}  // namespace android
