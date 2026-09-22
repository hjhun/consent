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
using Tizen.NUI;
using Tizen.NUI.BaseComponents;
using Tizen.NUI.Components;

namespace ConsentUI;

internal sealed class FeatureSettingsView : View
{
  private readonly FeatureSelection selection;
  private readonly Action<bool> submit;
  private readonly Action<Exception> failed;
  private readonly TextLabel title;
  private readonly TextLabel message;
  private readonly TextLabel pageLabel;
  private readonly Button[] features = new Button[3];
  private readonly Button mode;
  private readonly Button task;
  private readonly Button taskOnly;
  private readonly Button previous;
  private readonly Button next;
  private readonly Button review;
  private readonly Button run;
  private readonly Button language;
  private readonly Button cancel;
  private int page;
  private bool korean;
  private bool busy;
  private bool uncertain;
  public bool TaskOnly { get; private set; }
  public bool Executing { get; private set; }
  private string status = "";
  public FeatureSelection Selection => selection;
  public bool Korean => korean;
  public FeatureSettingsView(FeatureSelection selection, bool korean, Action<bool> submit, Action close, Action<Exception> failed)
  {
    this.selection = selection; this.korean = korean; this.submit = submit; this.failed = failed;
    Size = new Size(824, 624); BackgroundColor = Color.White; CornerRadius = 24;
    ParentOrigin = Tizen.NUI.ParentOrigin.TopLeft; PivotPoint = Tizen.NUI.PivotPoint.TopLeft; PositionUsesPivotPoint = true;
    FocusableChildren = true;
    title = Label(36, 20, 554, 48, 30);
    language = Button(622, 20, 166, 50, () => { this.korean = !this.korean; Render(); });
    for (int i = 0; i < features.Length; ++i)
    {
      int row = i;
      features[i] = Button(36, 86 + i * 58, 752, 52, () => Toggle(row));
    }
    mode = Button(36, 264, 360, 52, () => { selection.NextMode(); status = ""; Render(); });
    taskOnly = Button(428, 264, 360, 52, () => { TaskOnly = !TaskOnly; Render(); });
    task = Button(36, 326, 752, 52, () => { selection.NextTask(); status = ""; Render(); });
    message = Label(36, 388, 752, 64, 20);
    previous = Button(36, 466, 156, 48, () => { --page; Render(); });
    next = Button(632, 466, 156, 48, () => { ++page; Render(); });
    pageLabel = Label(214, 470, 396, 44, 22); pageLabel.HorizontalAlignment = HorizontalAlignment.Center;
    cancel = Button(36, 538, 176, 56, close);
    review = Button(228, 538, 268, 56, () => submit(false));
    run = Button(512, 538, 276, 56, () => submit(true));
    Render();
  }
  public void SetExecuting(bool value) { Executing = value; Render(); }
  public void SetBusy(bool value) { busy = value; Render(); }
  public void SetUncertain() { busy = false; uncertain = true; status = "UNCERTAIN"; Render(); }
  public void SetStatus(string decision)
  { busy = false; uncertain = false; status = decision; Render(); }
  private void Toggle(int row)
  {
    if (busy || uncertain) return;
    int index = page * 3 + row;
    if (index >= selection.Catalog.Features.Count) return;
    selection.Toggle(selection.Catalog.Features[index].Id); status = ""; Render();
  }
  private void Render()
  {
    title.Text = korean ? "선택 기능 설정" : "Optional features";
    language.Text = korean ? "English" : "한국어";
    for (int i = 0; i < features.Length; ++i)
    {
      int index = page * 3 + i;
      bool exists = index < selection.Catalog.Features.Count;
      if (exists)
      {
        var feature = selection.Catalog.Features[index];
        features[i].Text = (selection.IsSelected(feature.Id) ? "[x] " : "[ ] ") +
            (korean ? feature.TitleKorean : feature.TitleEnglish);
        features[i].Show();
      }
      else features[i].Hide();
      Enabled(features[i], !busy && !uncertain && exists,
          exists && selection.IsSelected(selection.Catalog.Features[index].Id));
    }
    mode.Text = selection.Mode switch
    {
      "SESSION" => korean ? "기간: 이번 대화" : "Period: this conversation",
      "TIMED" => korean ? "기간: 30분" : "Period: 30 minutes",
      _ => korean ? "승인 기간 선택" : "Choose approval period",
    };
    taskOnly.Text = (TaskOnly ? "[x] " : "[ ] ") + (korean ? "이번 작업만 한 번" : "This task only: once");
    task.Text = selection.TaskId switch
    {
      "calendar-summary" => korean ? "작업: 일정 요약" : "Task: calendar summary",
      "device-off" => korean ? "작업: 기기 끄기" : "Task: turn device off",
      "calendar-and-device" => korean ? "작업: 일정과 기기" : "Task: calendar + device",
      "calendar-alternative" => korean ? "작업: 기존 일정 데이터 대안" : "Task: use existing calendar data",
      "calendar-expanded" => korean ? "작업: 30일 일정 요청" : "Task: request 30 days of calendar",
      "conversation-close" => korean ? "작업: 대화 종료 및 정리" : "Task: end conversation and clean up",
      _ => throw new InvalidOperationException("Unsupported settings task"),
    };
    message.Text = status switch
    {
      "CLEANED" => korean ? "대화를 종료했습니다. 보관 데이터 정리 완료를 확인했습니다." : "Conversation closed. Holder cleanup completion was confirmed.",
      "CLEANUP" => korean ? "대화를 종료하고 보관 데이터를 정리하는 중…" : "Closing the conversation and cleaning up retained data…",
      "ALLOWED" => korean ? "허용 상태를 확인했습니다. 실행 시 권한을 다시 확인합니다." : "Approval confirmed. Each action checks authorization again.",
      "DENIED" => korean ? "거절되었습니다. 새 요청에는 다시 확인이 필요합니다." : "Denied. A new request requires confirmation again.",
      "INVALIDATED" => korean ? "설정이나 권한이 변경되었습니다. 다시 선택하세요." : "Selection or authorization changed. Review your choices again.",
      "EXECUTING" => korean ? "실행 권한을 확인하는 중입니다. 기능 선택을 변경하거나 해제할 수 있습니다." : "Authoritative execution is pending. You can change or clear the selected features.",
      "PENDING" => korean ? "승인 요청을 준비하는 중…" : "Preparing the approval request…",
      "UNCERTAIN" => korean ? "응답을 확인하지 못했습니다. 같은 요청을 재시도하거나 닫으세요." : "Reply unknown. Retry the same request or close; do not start another action.",
      "ERROR" => korean ? "요청을 처리하지 못했습니다. 상태 확인 후 다시 선택하세요." : "Request failed. Check the refreshed state before trying again.",
      _ => korean ? "기본은 미선택입니다. 선택만으로 허용되지 않습니다." : "Nothing is selected by default. Selection alone is not approval.",
    };
    previous.Text = korean ? "이전" : "Previous"; next.Text = korean ? "다음" : "Next";
    pageLabel.Text = $"{page + 1} / {(selection.Catalog.Features.Count + 2) / 3}";
    cancel.Text = korean ? "닫기" : "Close";
    review.Text = uncertain ? (korean ? "같은 요청 재시도" : "Retry same request") :
        korean ? "선택 기능 검토" : "Review selected features";
    run.Text = korean ? "작업 요청" : "Request task";
    Enabled(previous, !busy && !uncertain && page > 0); Enabled(next, !busy && !uncertain && (page + 1) * 3 < selection.Catalog.Features.Count);
    Enabled(mode, !busy && !uncertain); Enabled(task, !busy && !uncertain && !Executing);
    Enabled(taskOnly, !busy && !uncertain && !Executing, TaskOnly); Enabled(language, !busy && !uncertain);
    Enabled(review, !busy && (uncertain || selection.Ready));
    Enabled(run, !busy && !uncertain && !Executing && (selection.TaskId == "conversation-close" || TaskOnly ||
        (!selection.Dirty && selection.HasSelection)));
    Enabled(cancel, true);
  }
  private TextLabel Label(float x, float y, float width, float height, float pixels)
  {
    var label = new TextLabel { Position = new Position(x, y), Size = new Size(width, height),
      PixelSize = pixels, TextColor = new Color("#17171BFF"), MultiLine = true,
      EnableMarkup = false, Ellipsis = false, LineWrapMode = LineWrapMode.Character };
    Add(label); return label;
  }
  private Button Button(float x, float y, float width, float height, Action action)
  {
    var button = new Button { Position = new Position(x, y), Size = new Size(width, height),
      CornerRadius = 12, BackgroundColor = new Color("#E8EDF8FF"), TextColor = new Color("#152D68FF") };
    button.TextLabel.PixelSize = 20; button.TextLabel.EnableMarkup = false; button.TextLabel.Ellipsis = false;
    button.Clicked += (_, _) =>
    {
      try { if (button.IsEnabled && (!busy || ReferenceEquals(button, cancel))) action(); }
      catch (Exception error) { failed(error); }
    };
    Add(button); return button;
  }
  private static void Enabled(Button button, bool enabled, bool selected = false)
  {
    button.IsEnabled = enabled; button.Opacity = enabled ? 1f : 0.45f;
    button.BackgroundColor = new Color(enabled ? (selected ? "#BED5FFFF" : "#E8EDF8FF") : "#ECEDEFFF");
  }
}
