# Copyright (C) 2017 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

TOP_PATH := $(call my-dir)
include $(TOP_PATH)/third-part/ini-parser/Android.mk
include $(TOP_PATH)/third-part/hev-task-system/Android.mk

LOCAL_PATH = $(TOP_PATH)
include $(CLEAR_VARS)
LOCAL_MODULE    := hev-socks5-server
LOCAL_SRC_FILES := \
	src/hev-main.c \
	src/hev-config.c \
	src/hev-dns-query.c \
	src/hev-socks5-server.c \
	src/hev-socks5-session.c
LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/third-part/ini-parser/src \
	$(LOCAL_PATH)/third-part/hev-task-system/include
ifeq ($(TARGET_ARCH_ABI),armeabi-v7a)
LOCAL_CFLAGS += -mfpu=neon
endif
LOCAL_STATIC_LIBRARIES := ini-parser hev-task-system
include $(BUILD_EXECUTABLE)

