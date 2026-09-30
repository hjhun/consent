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

internal sealed class ConsentApplication : NUIApplication {
  private SynchronizationContext? ui;
  private ConsentWorker? worker;
  private readonly List<ConsentWorker> retiredWorkers = new();
  private FeatureWorker? featureWorker;
  private FeatureSettingsView? settings;
  private FeatureStatus? activeFeatureRequest;
  private string completedFeatureRequest = "";
  private FeatureReview? settingsReview;
  private FeatureSubmission? pendingSubmission;
  private bool featurePolling;
  private bool featureBusy;
  private bool recoveringFeatureRevision;
  private long lastFeaturePoll;
  private readonly Stopwatch featureClock = Stopwatch.StartNew();
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
  private TextLabel? title;
  private TextLabel? choiceNotice;
  private CheckBox? always;
  private bool settingChoice;
  private PromptSnapshot? nativeSnapshot;
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

  public ConsentApplication()
      : base("ConsentUI", WindowMode.Transparent, WindowType.Dialog) {}

  protected override void OnCreate() {
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

  protected override void OnAppControlReceived(
      AppControlReceivedEventArgs args) {
    base.OnAppControlReceived(args);
    if (closing)
      return;
    try {
      phase = UiPhase.InitialLaunch;
      var launch = launches.ReadInitial(() => {
        var control = args.ReceivedAppControl;
        control.ExtraData.TryGet("request_id", out string id);
        string requestedLocale = "en-US";
        if (control.ExtraData.TryGet("locale", out string value))
          requestedLocale = value;
        return new LaunchRequest(control.Operation, id, requestedLocale);
      });
      if (launch is null)
        return;
      locale = launch.Locale!;
      if (launch.Settings)
        BeginSettings();
      else
        BeginPrompt(launch.RequestId!);
    } catch (Exception error) {
      Fail(error);
    }
  }

  private void BeginPrompt(string id) {
    requestId = id;
    nativeSnapshot = null;
    review.Clear();
    busy = false;
    lifetime.Restart();
    settings?.Hide();
    card!.Show();
    UpdateLabels();
    worker = new ConsentWorker(error => PostUi(() => Fail(error)));
    timer?.Start();
    Refresh();
  }

  private void BeginSettings() {
    featureWorker =
        new FeatureWorker(error => PostUi(() => FeatureFailed(error)));
    LoadSettings();
  }

  private void LoadSettings() {
    if (featureWorker is null) {
      Close();
      return;
    }
    featureBusy = true;
    settings?.SetBusy(true);
    if (!featureWorker.Catalog(catalog => PostUi(() => {
                                 if (settings is not null) {
                                   root!.Remove(settings);
                                   settings.Dispose();
                                 }
                                 settings = new FeatureSettingsView(
                                     new FeatureSelection(catalog), Korean,
                                     SubmitFeatures, Close, Fail);
                                 settings.SetBusy(true);
                                 root!.Add(settings);
                                 card!.Hide();
                                 Resize();
                                 PollFeatures();
                               })))
      Close();
  }

  private void SubmitFeatures(bool task) {
    if (closing || featureBusy || settings is null || featureWorker is null)
      return;
    locale = settings.Korean ? "ko-KR" : "en-US";
    if (pendingSubmission is not null) {
      // The explicit retry control keeps the complete old command and epoch.
      ExecuteSubmission();
      return;
    }
    if (task && settings.Executing)
      return;
    if (task && settings.Selection.TaskId == "conversation-close") {
      pendingSubmission =
          new FeatureSubmission(settings.Selection, true, settings.TaskOnly);
      ExecuteSubmission();
      return;
    }
    if (!task && !settings.Selection.Ready)
      return;
    featurePolling = false;
    settingsReview = new FeatureReview(settings.Selection, Korean, task,
                                       settings.TaskOnly, settings.Executing);
    settings.Hide();
    card!.Show();
    lifetime.Restart();
    body!.Text = settingsReview.Pages[0];
    EnsureTextFits();
    UpdateLabels();
    UpdateButtons();
  }

  private void ExecuteSubmission() {
    if (pendingSubmission is null || featureWorker is null || settings is null)
      return;
    featureBusy = true;
    featurePolling = true;
    lifetime.Restart();
    settings.SetBusy(true);
    var submitted = pendingSubmission;
    if (!featureWorker.Submit(
            submitted, status => PostUi(() => {
                         if (!submitted.IsTask &&
                             (submitted.Ids.Length == 0 || submitted.SaveOnly))
                           featurePolling = false;
                         pendingSubmission = null;
                         ApplyFeatureStatus(status);
                       })))
      FeatureFailed(new InvalidOperationException("Feature queue is full"));
  }

  private void PollFeatures() {
    if (closing || featureWorker is null)
      return;
    featureBusy = true;
    settings?.SetBusy(true);
    if (!featureWorker.Status(status =>
                                  PostUi(() => ApplyFeatureStatus(status))))
      FeatureFailed(new InvalidOperationException("Feature queue is full"));
  }

  private void ApplyFeatureStatus(FeatureStatus status) {
    if (settings is null)
      throw new InvalidOperationException("Missing feature settings");
    if (status.CoordinatorEpoch !=
            settings.Selection.Catalog.CoordinatorEpoch ||
        status.CatalogRevision != settings.Selection.Catalog.Revision) {
      // A restart invalidates old command readiness even if revision restarts
      // at 1.
      pendingSubmission = null;
      featurePolling = false;
      if (recoveringFeatureRevision) {
        Close();
        return;
      }
      recoveringFeatureRevision = true;
      LoadSettings();
      return;
    }
    settings.Selection.Reconcile(status, recoveringFeatureRevision);
    settings.SetExecuting(status.ExecutionPending);
    featureBusy = false;
    lastFeaturePoll = featureClock.ElapsedMilliseconds;
    settings.SetStatus(recoveringFeatureRevision ? "INVALIDATED"
                       : status.CleanupComplete  ? "CLEANED"
                       : status.CleanupState.Length != 0 &&
                               status.Decision == "PENDING"
                           ? "CLEANUP"
                       : status.ExecutionPending ? "EXECUTING"
                                                 : status.Decision);
    recoveringFeatureRevision = false;
    if (featurePolling && status.Decision == "PENDING" &&
        status.RequestId.Length != 0 &&
        status.RequestId != completedFeatureRequest) {
      activeFeatureRequest = status;
      featurePolling = false;
      BeginPrompt(status.RequestId);
    } else {
      featurePolling =
          status.ExecutionPending ||
          (featurePolling && status.Decision is "IDLE" or "PENDING");
      settings.SetBusy(featurePolling && !status.ExecutionPending);
    }
  }

  private void FeatureFailed(Exception error) {
    ConsentWorker.LogStatus("feature", error);
    if (recoveringFeatureRevision) {
      recoveringFeatureRevision = false;
      Close();
      return;
    }
    featureBusy = false;
    featurePolling = false;
    if (settings is null) {
      Close();
      return;
    }
    if (error is NativeFailure { Status : -116 or - 17 or - 22 or - 13 }) {
      // Definite rejection is reconciled read-only. No new command is
      // generated.
      pendingSubmission = null;
      recoveringFeatureRevision = true;
      LoadSettings();
    } else if (pendingSubmission is not null)
      settings.SetUncertain();
    else
      settings.SetStatus("ERROR");
  }

  private void FinishPrompt(string decision) {
    if (settings is null) {
      Close();
      return;
    }
    if (worker is not null) {
      worker.Stop();
      retiredWorkers.Add(worker);
      worker = null;
    }
    completedFeatureRequest = requestId;
    review.Clear();
    activeFeatureRequest = null;
    card!.Hide();
    settings.Show();
    settings.SetStatus(decision);
    busy = false;
    featurePolling = true;
    timer?.Start();
    PollFeatures();
  }

  private void PostUi(Action action) => ui?.Post(
      _ => {
        if (!closing) {
          try {
            action();
          } catch (Exception error) {
            Fail(error);
          }
        }
      },
      null);

  private void Refresh(string? requestedLocale = null) {
    if (closing || busy || worker is null)
      return;
    busy = true;
    phase = UiPhase.NativeFetch;
    UpdateButtons();
    if (!worker.Fetch(requestId, requestedLocale ?? locale,
                      fresh => PostUi(() => Display(fresh))))
      Close();
  }

  private void Display(PromptSnapshot fresh) {
    // Construct/validate pages before publishing the token to the controls.
    // Same text refreshes preserve review progress, new content resets it.
    phase = UiPhase.PromptDisplay;
    if (activeFeatureRequest is not null &&
        !activeFeatureRequest.Matches(fresh))
      throw new InvalidOperationException("Feature prompt binding mismatch");
    nativeSnapshot = fresh;
    if (snapshot is not null && snapshot.AlwaysAllowed &&
        snapshot.SameBinding(fresh))
      fresh = fresh.WithChoice(true);
    settingChoice = true;
    always!.IsSelected = fresh.AlwaysAllowed;
    settingChoice = false;
    body!.Text = fresh.Pages[review.PageFor(fresh)];
    EnsureTextFits();
    review.Install(fresh);
    locale = fresh.Locale;
    busy = false;
    lastRefresh = lifetime.ElapsedMilliseconds;
    UpdateLabels();
    UpdateButtons();
  }

  private void EnsureTextFits() {
    // Dali NaturalSize uses MAX_FLOAT width; it is not the constrained layout.
    // Character wrapping and a bounded single-grapheme width prevent horizontal
    // clipping. GetHeightForWidth synchronously processes pending text changes.
    float height = body!.GetHeightForWidth(PopupTextMetrics.Width);
    if (!reportedFit) {
      // One numeric-only diagnostic reproduces the old 18pt measurement to
      // distinguish DPI conversion from natural-width rejection on the target.
      glyphMeasure!.MultiLine = true;
      glyphMeasure.Text = body.Text;
      glyphMeasure.PointSize = 18;
      float oldWidth = glyphMeasure.NaturalSize.Width;
      float oldHeight = glyphMeasure.GetHeightForWidth(PopupTextMetrics.Width);
      Console.WriteLine(FormattableString.Invariant(
          $"ConsentUI fit phase={
    phase
  } pixels={
    PopupTextMetrics.FontPixels
  } height={
    height:F1} bound_height={
    PopupTextMetrics.Height
  } legacy18pt_natural_width={
    oldWidth:F1} legacy18pt_height={
    oldHeight:F1}"));
      reportedFit = true;
      glyphMeasure.MultiLine = false;
      glyphMeasure.PixelSize = PopupTextMetrics.FontPixels;
    }
    if (!PopupTextMetrics.HeightFits(height)) {
      Console.Error.WriteLine(FormattableString.Invariant(
          $"ConsentUI fit phase={phase} height={height:F1}"));
      throw new UiFailure(UiFailureReason.TextHeight);
    }
    var elements = StringInfo.GetTextElementEnumerator(body.Text);
    while (elements.MoveNext()) {
      string element = elements.GetTextElement();
      if (element == "\n" || measuredGlyphs.Contains(element))
        continue;
      glyphMeasure!.Text = element;
      float width = glyphMeasure.NaturalSize.Width;
      if (!PopupTextMetrics.GlyphFits(width)) {
        Console.Error.WriteLine(FormattableString.Invariant(
            $"ConsentUI fit phase={phase} glyph_width={width:F1}"));
        throw new UiFailure(UiFailureReason.GlyphWidth);
      }
      if (measuredGlyphs.Count >= 1024)
        measuredGlyphs.Clear();
      measuredGlyphs.Add(element);
    }
  }

  private void Choose(bool approved) {
    if (settingsReview is not null) {
      if (closing || busy || (approved && !settingsReview.CanApply))
        return;
      if (approved && lifetime.ElapsedMilliseconds < 60000)
        pendingSubmission = settingsReview.Submission;
      settingsReview = null;
      card!.Hide();
      settings!.Show();
      UpdateButtons();
      if (pendingSubmission is not null)
        ExecuteSubmission();
      else
        featurePolling = settings.Executing;
      return;
    }
    if (closing || busy || worker is null || snapshot is null)
      return;
    if (approved && (!snapshot.CanApprove || reviewed != snapshot.Pages.Count))
      return;
    if (lifetime.ElapsedMilliseconds >= 60000) {
      Close();
      return;
    }
    busy = true;
    timer?.Stop();
    phase = UiPhase.Response;
    UpdateButtons();
    var displayed = nativeSnapshot ??
        throw new InvalidOperationException("Missing native snapshot");
    bool persistent = snapshot.AlwaysAllowed;
    if (!worker.Respond(displayed, approved,
                        decision => PostUi(() => {
                          Console.WriteLine(
                              $"ConsentUI response decision={decision}");
                          FinishPrompt(decision);
                        }), persistent))
      Close();
  }

  private bool Tick(object? sender, Tizen.NUI.Timer.TickEventArgs args) {
    if (closing)
      return false;
    if (settingsReview is not null && lifetime.ElapsedMilliseconds >= 60000) {
      Choose(false);
      return true;
    }
    if (featurePolling && !featureBusy && lifetime.ElapsedMilliseconds >= 60000)
      FeatureFailed(new NativeFailure(-110));
    if (worker is not null && lifetime.ElapsedMilliseconds >= 60000) {
      Close();
      return false;
    }
    if (worker is not null && !busy &&
        lifetime.ElapsedMilliseconds - lastRefresh >= 500)
      Refresh();
    if (worker is null && featurePolling && !featureBusy &&
        featureClock.ElapsedMilliseconds - lastFeaturePoll >= 250)
      PollFeatures();
    for (int i = retiredWorkers.Count - 1; i >= 0; --i)
      if (retiredWorkers[i].Join(0))
        retiredWorkers.RemoveAt(i);
    if (retiredWorkers.Count >= 16) {
      Close();
      return false;
    }
    return true;
  }

  private void MovePage(int offset) {
    if (settingsReview is not null) {
      if (closing || busy)
        return;
      settingsReview.Move(offset);
      body!.Text = settingsReview.Pages[settingsReview.Page];
      EnsureTextFits();
      UpdateLabels();
      UpdateButtons();
      return;
    }
    if (closing || busy || snapshot is null)
      return;
    int target = page + offset;
    if (target < 0 || target >= snapshot.Pages.Count)
      return;
    phase = UiPhase.PageNavigation;
    body!.Text = snapshot.Pages[target];
    EnsureTextFits();
    review.Move(offset);
    UpdateLabels();
    UpdateButtons();
  }

  private void BuildView() {
    root = new View {
      BackgroundColor = new Color("#00000099"),
      FocusableChildren = true,
      ParentOrigin = ParentOrigin.TopLeft,
      PivotPoint = PivotPoint.TopLeft,
      PositionUsesPivotPoint = true,
      Position = new Position(0, 0),
    };
    root.TouchEvent += (_, _) => true;
    card = new View {
      Size = new Size(580, 600),
      BackgroundColor = Color.White,
      CornerRadius = 28,
      FocusableChildren = true,
      // Scale about the same top-left point used by Resize()'s centered offset.
      // The default center pivot otherwise shifts half the scale growth
      // offscreen.
      ParentOrigin = ParentOrigin.TopLeft,
      PivotPoint = PivotPoint.TopLeft,
      PositionUsesPivotPoint = true,
    };
    title = Label("ConsentUI", 28, 24, 380, 40, 26);
    card.Add(title);
    language = MakeButton("한국어", 426, 24, 126,
                          () => Refresh(Korean ? "en-US" : "ko-KR"));
    notice = Label("Waiting for a consent request…", 28, 78, 524, 44, 15);
    card.Add(notice);
    var context = new View {
      Position = new Position(28, 132),
      Size = new Size(524, 228),
      BackgroundColor = new Color("#F3F4F6FF"),
      CornerRadius = 18,
    };
    card.Add(context);
    body = Label("", 44, 144, PopupTextMetrics.Width, PopupTextMetrics.Height,
                 PopupTextMetrics.FontPixels);
    glyphMeasure = Label("", 0, 0, PopupTextMetrics.Width,
                         PopupTextMetrics.Height, PopupTextMetrics.FontPixels);
    glyphMeasure.MultiLine = false;
    glyphMeasure.Hide();
    root.Add(glyphMeasure);
    card.Add(body);
    previous = MakeButton("Previous", 28, 370, 108, () => MovePage(-1));
    next = MakeButton("Next", 444, 370, 108, () => MovePage(1));
    pageLabel = Label("", 148, 382, 284, 28, 15);
    pageLabel.HorizontalAlignment = HorizontalAlignment.Center;
    card.Add(pageLabel);
    always = new CheckBox {
      Text = "항상 허용",
      Position = new Position(28, 426),
      Size = new Size(524, 44),
      IsSelected = false,
    };
    always.TextLabel.PixelSize = 17;
    always.SelectedChanged += (_, _) => {
      if (settingChoice || closing || busy || snapshot is null)
        return;
      try {
        var selected = snapshot.WithChoice(always.IsSelected);
        body!.Text = selected.Pages[0];
        EnsureTextFits();
        review.Install(selected);
        UpdateLabels();
        UpdateButtons();
      } catch (Exception error) {
        Fail(error);
      }
    };
    card.Add(always);
    choiceNotice = Label("", 28, 476, 524, 40, 13);
    card.Add(choiceNotice);
    deny = MakeButton("Deny", 28, 528, 252, () => Choose(false));
    allow = MakeButton("Allow once", 300, 528, 252, () => Choose(true));
    root.Add(card);
    Window.Default.Add(root);
    Resize();
    UpdateButtons();
    FocusManager.Instance.SetCurrentFocusView(deny);
  }

  private static TextLabel Label(string text, float x, float y, float width,
                                 float height, float pixels) => new() {
    Text = text,
    Position = new Position(x, y),
    Size = new Size(width, height),
    PixelSize = pixels,
    TextColor = new Color("#17171BFF"),
    MultiLine = true,
    EnableMarkup = false,
    Ellipsis = false,
    LineWrapMode = LineWrapMode.Character,
    VerticalAlignment = VerticalAlignment.Top,
  };

  private Button MakeButton(string text, float x, float y, float width,
                            Action action) {
    var button = new Button {
      Text = text,
      Position = new Position(x, y),
      Size = new Size(width, 48),
      BackgroundColor = new Color("#E8EDF8FF"),
      TextColor = new Color("#152D68FF"),
      CornerRadius = 24,
    };
    button.TextLabel.PixelSize = 16;
    button.TextLabel.EnableMarkup = false;
    button.TextLabel.Ellipsis = false;
    button.Clicked += (_, _) => {
      try {
        action();
      } catch (Exception error) {
        Fail(error);
      }
    };
    card!.Add(button);
    return button;
  }

  private void UpdateLabels() {
    if (notice is null)
      return;
    language!.Text = Korean ? "English" : "한국어";
    always!.Text = Korean ? "항상 허용" : "Always allow";
    title!.Text = snapshot?.Fields.GetValueOrDefault("r0.feature_id") ==
                  "calendar.read" && snapshot.TotalCount == 1
        ? (Korean ? "달력 접근을 허용할까요?" : "Allow calendar access?")
        : (Korean ? "접근을 허용할까요?" : "Allow access?");
    choiceNotice!.Text = snapshot?.CanChoosePersistent == true
        ? (Korean ? "현재 프로필의 같은 조건에만 적용됩니다. 보관 기간은 늘어나지 않습니다."
                  : "Same profile and exact conditions. Retention is unchanged.")
        : (Korean ? "이 요청과 정책은 항상 허용을 지원하지 않습니다."
                  : "Always allow is unavailable for this request or policy.");
    previous!.Text = Korean ? "이전" : "Previous";
    next!.Text = Korean ? "다음" : "Next";
    if (settingsReview is not null) {
      deny!.Text = Korean ? "돌아가기" : "Back";
      allow!.Text =
          settingsReview.Submission.SaveOnly
              ? (Korean ? "변경한 설정 저장" : "Save changed settings")
          : settingsReview.Submission.IsTask
              ? (Korean ? "검토한 작업 요청" : "Request reviewed task")
          : Korean ? "저장하고 승인 검토"
                   : "Save and review approval";
      notice.Text =
          settingsReview.Submission.IsTask
              ? (Korean
                     ? "작업의 전체 범위와 기간을 확인하세요. 필요한 경우 별도 동의 화면이 열립니다."
                     : ("Review the complete task scope and period. Mi" +
    "ssing approvals open a separate consent scree" +
    "n."))
          : Korean
              ? "설정 내용을 모두 확인하세요. 저장만으로 접근이 허용되지는 않습니다."
              : ("Review the complete settings. Saving alone do" +
    "es not authorize access.");
      pageLabel!.Text =
          $"{settingsReview.Page + 1} / {settingsReview.Pages.Count}";
      return;
    }
    deny!.Text = Korean ? "거절" : "Deny";
    allow!.Text = snapshot?.AlwaysAllowed == true
                      ? (Korean ? "항상 허용" : "Always allow")
                  : snapshot?.VersionedApproval == true
                      ? (Korean ? "표시한 기간 허용" : "Allow as displayed")
                  : Korean ? "이번 한 번 허용"
                           : "Allow once";
    notice.Text =
        snapshot is null
            ? (Korean ? "동의 요청을 확인하는 중…" : "Checking the request…")
        : !snapshot.CanApprove
            ? (Korean
                   ? "이 요청의 승인 기간을 지원하지 않습니다. 거절만 가능합니다."
                   : ("This approval period is unsupported. Only den" +
    "ial is available."))
        : Korean
            ? "모든 페이지를 확인한 후 선택하세요. 닫거나 60초가 지나면 허용되지 않습니다."
            : ("Review every page before choosing. Closing or" +
    " waiting 60 seconds does not approve.");
    pageLabel!.Text =
        snapshot is null ? "" : $"{page + 1} / {snapshot.Pages.Count}";
  }

  private void UpdateButtons() {
    if (allow is null)
      return;
    if (settingsReview is not null) {
      always!.IsEnabled = false;
      bool available = !closing && !busy;
      SetEnabled(allow, available && settingsReview.CanApply);
      SetEnabled(deny!, available);
      SetEnabled(language!, false);
      SetEnabled(previous!, available && settingsReview.Page > 0);
      SetEnabled(next!, available && settingsReview.Page + 1 <
                                         settingsReview.Pages.Count);
      return;
    }
    bool ready = !closing && !busy && snapshot is not null;
    always!.IsEnabled = ready && snapshot!.CanChoosePersistent;
    SetEnabled(allow, ready && snapshot!.CanApprove &&
                          reviewed == snapshot.Pages.Count);
    if (allow.IsEnabled) {
      allow.BackgroundColor = new Color("#0072DEFF");
      allow.TextColor = Color.White;
    }
    SetEnabled(deny!, ready);
    SetEnabled(language!, ready);
    SetEnabled(previous!, ready && page > 0);
    SetEnabled(next!, ready && page + 1 < snapshot!.Pages.Count);
  }

  private static void SetEnabled(Button button, bool enabled) {
    button.IsEnabled = enabled;
    button.BackgroundColor = new Color(enabled ? "#F0F1F3FF" : "#ECEDEFFF");
    button.TextColor = new Color(enabled ? "#373B43FF" : "#737A85FF");
    // Explicit parent opacity keeps disabled state distinguishable even when
    // the installed component theme supplies the same enabled/disabled colors.
    button.Opacity = enabled ? 1.0f : 0.45f;
  }

  private void Resize() {
    if (root is null || card is null)
      return;
    var size = Window.Default.Size;
    root.Size = new Size(size.Width, size.Height);
    var geometry = PopupTextMetrics.Geometry(size.Width, size.Height);
    if (geometry.Scale <= 0)
      return;
    card.Scale = new Vector3(geometry.Scale, geometry.Scale, 1);
    card.Position = new Position(geometry.X, geometry.Y);
    if (settings is not null) {
      float settingsScale = Math.Min(size.Width / 864f,
                                     size.Height / 664f);
      settings.Scale = new Vector3(settingsScale, settingsScale, 1);
      settings.Position = new Position((size.Width - 824 * settingsScale) / 2,
          (size.Height - 624 * settingsScale) / 2);
    }
  }

  private void OnResize(object? sender, EventArgs args) => Resize();
  private void OnKey(object? sender, Window.KeyEventArgs args) {
    if (args.Key.State == Key.StateType.Down && args.Key.KeyPressedName is
                                                "XF86Back" or "Escape") {
      if (settingsReview is not null)
        Choose(false);
      else
        Close();
    }
  }

  private void Fail(Exception error) {
    if (error is PromptFinished finished) {
      FinishPrompt(finished.Decision);
      return;
    }
    ConsentWorker.LogStatus($"failure phase={phase}", error);
    Close();
  }

  private void Close() {
    if (closing)
      return;
    closing = true;
    timer?.Stop();
    UpdateButtons();
    worker?.Stop();
    featureWorker?.Stop();
    Exit();
  }

  protected override void OnPause() {
    if (worker is not null || featureWorker is not null)
      Close();
    base.OnPause();
  }

  protected override void OnTerminate() {
    closing = true;
    worker?.Stop();
    featureWorker?.Stop();
    timer?.Stop();
    timer?.Dispose();
    timer = null;
    Window.Default.KeyEvent -= OnKey;
    Window.Default.Resized -= OnResize;
    base.OnTerminate();
  }

  private static void Main(string[] args) {
    var app = new ConsentApplication();
    try {
      app.Run(args);
    } finally {
      app.worker?.Stop();
      app.featureWorker?.Stop();
      foreach (var retired in app.retiredWorkers) {
        retired.Stop();
        retired.Join(12000);
      }
      app.featureWorker?.Join(12000);
      // This runs after NUI has returned, never blocks its event dispatcher.
      if (app.worker is not null && !app.worker.Join(12000))
        Console.Error.WriteLine(
            "ConsentUI native shutdown did not complete within 12 seconds");
    }
  }
}
