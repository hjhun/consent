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
using System.Diagnostics;
using System.Globalization;
using Tizen.Applications;
using Tizen.NUI;
using Tizen.NUI.BaseComponents;
using Tizen.NUI.Components;

namespace ConsentUI;

internal sealed class ConsentApplication : NUIApplication
{
  private SynchronizationContext? ui;
  private ConsentWorker? worker;
  private Tizen.NUI.Timer? timer;
  private View? root;
  private View? card;
  private TextLabel? body;
  private TextLabel? glyphMeasure;
  private readonly HashSet<string> measuredGlyphs = new(StringComparer.Ordinal);
  private bool reportedFit;
  private UiPhase phase;
  private TextLabel? pageLabel;
  private TextLabel? notice;
  private Button? allow;
  private Button? deny;
  private Button? previous;
  private Button? next;
  private Button? language;
  private readonly PromptReview review = new();
  private readonly LaunchGate launches = new();
  private PromptSnapshot? snapshot => review.Snapshot;
  private int page => review.Page;
  private int reviewed => review.Reviewed;
  private string requestId = "";
  private string locale = "en-US";
  private readonly Stopwatch lifetime = new();
  private long lastRefresh;
  private bool busy;
  private bool closing;
  private bool Korean => locale.StartsWith("ko", StringComparison.Ordinal);

  public ConsentApplication() : base("ConsentUI", WindowMode.Transparent, WindowType.Dialog) { }

  protected override void OnCreate()
  {
    base.OnCreate();
    phase = UiPhase.Create;
    ui = SynchronizationContext.Current ?? new TizenSynchronizationContext();
    Window.Default.Title = "ConsentUI";
    Window.Default.KeyEvent += OnKey;
    Window.Default.Resized += OnResize;
    BuildView();
    timer = new Tizen.NUI.Timer(100);
    timer.Tick += Tick;
    timer.Start();
    lifetime.Start();
  }

  protected override void OnAppControlReceived(AppControlReceivedEventArgs args)
  {
    base.OnAppControlReceived(args);
    if (closing) return;
    try
    {
      phase = UiPhase.InitialLaunch;
      var launch = launches.ReadInitial(() =>
      {
        var control = args.ReceivedAppControl;
        control.ExtraData.TryGet("request_id", out string id);
        string requestedLocale = "en-US";
        if (control.ExtraData.TryGet("locale", out string value)) requestedLocale = value;
        return new LaunchRequest(control.Operation, id, requestedLocale);
      });
      if (launch is null) return;
      requestId = launch.RequestId!;
      locale = launch.Locale!;
      lifetime.Restart();
      UpdateLabels();
      worker = new ConsentWorker(error => PostUi(() => Fail(error)));
      Refresh();
    }
    catch (Exception error) { Fail(error); }
  }

  private void PostUi(Action action) => ui?.Post(_ =>
  {
    if (!closing)
    {
      try { action(); }
      catch (Exception error) { Fail(error); }
    }
  }, null);

  private void Refresh(string? requestedLocale = null)
  {
    if (closing || busy || worker is null) return;
    busy = true;
    phase = UiPhase.NativeFetch;
    UpdateButtons();
    if (!worker.Fetch(requestId, requestedLocale ?? locale, fresh => PostUi(() => Display(fresh)))) Close();
  }

  private void Display(PromptSnapshot fresh)
  {
    // Construct/validate pages before publishing the token to the controls.
    // Same text refreshes preserve review progress, new content resets it.
    phase = UiPhase.PromptDisplay;
    body!.Text = fresh.Pages[review.PageFor(fresh)];
    EnsureTextFits();
    review.Install(fresh);
    locale = fresh.Locale;
    busy = false;
    lastRefresh = lifetime.ElapsedMilliseconds;
    UpdateLabels();
    UpdateButtons();
  }

  private void EnsureTextFits()
  {
    // Dali NaturalSize uses MAX_FLOAT width; it is not the constrained layout.
    // Character wrapping and a bounded single-grapheme width prevent horizontal
    // clipping. GetHeightForWidth synchronously processes pending text changes.
    float height = body!.GetHeightForWidth(PopupTextMetrics.Width);
    if (!reportedFit)
    {
      // One numeric-only diagnostic reproduces the old 18pt measurement to
      // distinguish DPI conversion from natural-width rejection on the target.
      glyphMeasure!.MultiLine = true;
      glyphMeasure.Text = body.Text;
      glyphMeasure.PointSize = 18;
      float oldWidth = glyphMeasure.NaturalSize.Width;
      float oldHeight = glyphMeasure.GetHeightForWidth(PopupTextMetrics.Width);
      Console.WriteLine(FormattableString.Invariant(
          $"ConsentUI fit phase={phase} pixels={PopupTextMetrics.FontPixels} height={height:F1} bound_height={PopupTextMetrics.Height} legacy18pt_natural_width={oldWidth:F1} legacy18pt_height={oldHeight:F1}"));
      reportedFit = true;
      glyphMeasure.MultiLine = false;
      glyphMeasure.PixelSize = PopupTextMetrics.FontPixels;
    }
    if (!PopupTextMetrics.HeightFits(height))
    {
      Console.Error.WriteLine(FormattableString.Invariant($"ConsentUI fit phase={phase} height={height:F1}"));
      throw new UiFailure(UiFailureReason.TextHeight);
    }
    var elements = StringInfo.GetTextElementEnumerator(body.Text);
    while (elements.MoveNext())
    {
      string element = elements.GetTextElement();
      if (element == "\n" || measuredGlyphs.Contains(element)) continue;
      glyphMeasure!.Text = element;
      float width = glyphMeasure.NaturalSize.Width;
      if (!PopupTextMetrics.GlyphFits(width))
      {
        Console.Error.WriteLine(FormattableString.Invariant($"ConsentUI fit phase={phase} glyph_width={width:F1}"));
        throw new UiFailure(UiFailureReason.GlyphWidth);
      }
      if (measuredGlyphs.Count >= 1024) measuredGlyphs.Clear();
      measuredGlyphs.Add(element);
    }
  }

  private void Choose(bool approved)
  {
    if (closing || busy || worker is null || snapshot is null) return;
    if (approved && (!snapshot.CanAllowOnce || reviewed != snapshot.Pages.Count)) return;
    if (lifetime.ElapsedMilliseconds >= 60000) { Close(); return; }
    busy = true;
    timer?.Stop();
    phase = UiPhase.Response;
    UpdateButtons();
    var displayed = snapshot;
    if (!worker.Respond(displayed, approved, decision => PostUi(() =>
    {
      Console.WriteLine($"ConsentUI response decision={decision}");
      Close();
    }))) Close();
  }

  private bool Tick(object? sender, Tizen.NUI.Timer.TickEventArgs args)
  {
    if (closing) return false;
    if (lifetime.ElapsedMilliseconds >= 60000) { Close(); return false; }
    if (worker is not null && !busy && lifetime.ElapsedMilliseconds - lastRefresh >= 500) Refresh();
    return true;
  }

  private void MovePage(int offset)
  {
    if (closing || busy || snapshot is null) return;
    int target = page + offset;
    if (target < 0 || target >= snapshot.Pages.Count) return;
    phase = UiPhase.PageNavigation;
    body!.Text = snapshot.Pages[target];
    EnsureTextFits();
    review.Move(offset);
    UpdateLabels();
    UpdateButtons();
  }

  private void BuildView()
  {
    root = new View
    {
      BackgroundColor = new Color("#00000099"), FocusableChildren = true,
      ParentOrigin = ParentOrigin.TopLeft, PivotPoint = PivotPoint.TopLeft,
      PositionUsesPivotPoint = true, Position = new Position(0, 0),
    };
    root.TouchEvent += (_, _) => true;
    card = new View
    {
      Size = new Size(824, 624), BackgroundColor = Color.White,
      CornerRadius = 24, FocusableChildren = true,
      // Scale about the same top-left point used by Resize()'s centered offset.
      // The default center pivot otherwise shifts half the scale growth offscreen.
      ParentOrigin = ParentOrigin.TopLeft, PivotPoint = PivotPoint.TopLeft,
      PositionUsesPivotPoint = true,
    };
    card.Add(Label("ConsentUI", 36, 20, 550, 48, 32));
    language = MakeButton("한국어", 622, 20, 166,
        () => Refresh(Korean ? "en-US" : "ko-KR"));
    notice = Label("Waiting for a consent request…", 36, 78, 752, 72, 22);
    card.Add(notice);
    body = Label("", 36, 156, PopupTextMetrics.Width, PopupTextMetrics.Height, PopupTextMetrics.FontPixels);
    glyphMeasure = Label("", 0, 0, PopupTextMetrics.Width, PopupTextMetrics.Height, PopupTextMetrics.FontPixels);
    glyphMeasure.MultiLine = false;
    glyphMeasure.Hide();
    root.Add(glyphMeasure);
    card.Add(body);
    previous = MakeButton("Previous", 36, 450, 156, () => MovePage(-1));
    next = MakeButton("Next", 632, 450, 156, () => MovePage(1));
    pageLabel = Label("", 214, 456, 396, 48, 22);
    pageLabel.HorizontalAlignment = HorizontalAlignment.Center;
    card.Add(pageLabel);
    deny = MakeButton("Deny", 36, 538, 360, () => Choose(false));
    allow = MakeButton("Allow once", 428, 538, 360, () => Choose(true));
    root.Add(card);
    Window.Default.Add(root);
    Resize();
    UpdateButtons();
    FocusManager.Instance.SetCurrentFocusView(deny);
  }

  private static TextLabel Label(string text, float x, float y, float width, float height, float pixels) =>
      new()
      {
        Text = text, Position = new Position(x, y), Size = new Size(width, height),
        PixelSize = pixels, TextColor = new Color("#17171BFF"), MultiLine = true,
        EnableMarkup = false, Ellipsis = false, LineWrapMode = LineWrapMode.Character,
        VerticalAlignment = VerticalAlignment.Top,
      };

  private Button MakeButton(string text, float x, float y, float width, Action action)
  {
    var button = new Button
    {
      Text = text, Position = new Position(x, y), Size = new Size(width, 56),
      BackgroundColor = new Color("#E8EDF8FF"), TextColor = new Color("#152D68FF"),
      CornerRadius = 12,
    };
    button.TextLabel.PixelSize = 24;
    button.TextLabel.EnableMarkup = false;
    button.TextLabel.Ellipsis = false;
    button.Clicked += (_, _) =>
    {
      try { action(); }
      catch (Exception error) { Fail(error); }
    };
    card!.Add(button);
    return button;
  }

  private void UpdateLabels()
  {
    if (notice is null) return;
    language!.Text = Korean ? "English" : "한국어";
    previous!.Text = Korean ? "이전" : "Previous";
    next!.Text = Korean ? "다음" : "Next";
    deny!.Text = Korean ? "거절" : "Deny";
    allow!.Text = Korean ? "이번 한 번 허용" : "Allow once";
    notice.Text = snapshot is null ? (Korean ? "동의 요청을 확인하는 중…" : "Checking the request…") :
        !snapshot.CanAllowOnce ? (Korean ? "이 요청은 일회 허용을 지원하지 않습니다. 거절만 가능합니다." :
          "This request does not support a one-time grant. Only denial is available.") :
        Korean ? "모든 페이지를 확인한 후 선택하세요. 닫거나 60초가 지나면 허용되지 않습니다." :
          "Review every page before choosing. Closing or waiting 60 seconds does not approve.";
    pageLabel!.Text = snapshot is null ? "" : $"{page + 1} / {snapshot.Pages.Count}";
  }

  private void UpdateButtons()
  {
    if (allow is null) return;
    bool ready = !closing && !busy && snapshot is not null;
    SetEnabled(allow, ready && snapshot!.CanAllowOnce && reviewed == snapshot.Pages.Count);
    SetEnabled(deny!, ready);
    SetEnabled(language!, ready);
    SetEnabled(previous!, ready && page > 0);
    SetEnabled(next!, ready && page + 1 < snapshot!.Pages.Count);
  }

  private static void SetEnabled(Button button, bool enabled)
  {
    button.IsEnabled = enabled;
    button.BackgroundColor = new Color(enabled ? "#E8EDF8FF" : "#ECEDEFFF");
    button.TextColor = new Color(enabled ? "#152D68FF" : "#737A85FF");
    // Explicit parent opacity keeps disabled state distinguishable even when
    // the installed component theme supplies the same enabled/disabled colors.
    button.Opacity = enabled ? 1.0f : 0.45f;
  }

  private void Resize()
  {
    if (root is null || card is null) return;
    var size = Window.Default.Size;
    root.Size = new Size(size.Width, size.Height);
    float scale = Math.Min(size.Width / 864f, size.Height / 664f);
    if (scale <= 0) return;
    card.Scale = new Vector3(scale, scale, 1);
    card.Position = new Position((size.Width - 824 * scale) / 2, (size.Height - 624 * scale) / 2);
  }

  private void OnResize(object? sender, EventArgs args) => Resize();
  private void OnKey(object? sender, Window.KeyEventArgs args)
  {
    if (args.Key.State == Key.StateType.Down && args.Key.KeyPressedName is "XF86Back" or "Escape") Close();
  }

  private void Fail(Exception error)
  {
    ConsentWorker.LogStatus($"failure phase={phase}", error);
    Close();
  }

  private void Close()
  {
    if (closing) return;
    closing = true;
    timer?.Stop();
    UpdateButtons();
    worker?.Stop();
    Exit();
  }

  protected override void OnPause()
  {
    if (worker is not null) Close();
    base.OnPause();
  }

  protected override void OnTerminate()
  {
    closing = true;
    worker?.Stop();
    timer?.Stop();
    timer?.Dispose();
    timer = null;
    Window.Default.KeyEvent -= OnKey;
    Window.Default.Resized -= OnResize;
    base.OnTerminate();
  }

  private static void Main(string[] args)
  {
    var app = new ConsentApplication();
    try { app.Run(args); }
    finally
    {
      app.worker?.Stop();
      // This runs after NUI has returned, never blocks its event dispatcher.
      if (app.worker is not null && !app.worker.Join(12000))
        Console.Error.WriteLine("ConsentUI native shutdown did not complete within 12 seconds");
    }
  }
}
