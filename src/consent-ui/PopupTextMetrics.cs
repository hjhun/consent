/*
 * Copyright (c) 2026 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
namespace ConsentUI;

internal static class PopupTextMetrics {
  public const float Width = 492;
  public const float Height = 204;
  public const float FontPixels = 16;
  public static (float Scale, float X, float Y) Geometry(float width,
                                                        float height) {
    float scale = Math.Max(0, Math.Min(1,
        Math.Min((width - 48) / 580, (height - 48) / 600)));
    float x = (width - 580 * scale) / 2;
    float y = width <= 600 ? height - 600 * scale - 24
                          : (height - 600 * scale) / 2;
    return (scale, x, y);
  }

  public static bool HeightFits(float height) =>
      float.IsFinite(height) && height > 0 && height <= Height;
  public static bool GlyphFits(float width) =>
      float.IsFinite(width) && width >= 0 && width <= Width;
}

internal enum UiPhase {
  Create,
  InitialLaunch,
  NativeFetch,
  PromptDisplay,
  PageNavigation,
  Response
}
internal enum UiFailureReason { TextHeight, GlyphWidth }
internal sealed class UiFailure : Exception {
  public UiFailureReason Reason { get; }
  public UiFailure(UiFailureReason reason) : base(reason.ToString()) {
    Reason = reason;
  }
}
