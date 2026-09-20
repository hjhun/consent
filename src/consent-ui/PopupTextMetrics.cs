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

internal static class PopupTextMetrics
{
  public const float Width = 752;
  public const float Height = 290;
  public const float FontPixels = 24;
  public static bool HeightFits(float height) => float.IsFinite(height) && height > 0 && height <= Height;
  public static bool GlyphFits(float width) => float.IsFinite(width) && width >= 0 && width <= Width;
}

internal enum UiPhase { Create, InitialLaunch, NativeFetch, PromptDisplay, PageNavigation, Response }
internal enum UiFailureReason { TextHeight, GlyphWidth }
internal sealed class UiFailure : Exception
{
  public UiFailureReason Reason { get; }
  public UiFailure(UiFailureReason reason) : base(reason.ToString()) { Reason = reason; }
}
