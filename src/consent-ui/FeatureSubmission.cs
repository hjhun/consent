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

// The complete mutation and command IDs survive unknown replies. Retry uses
// this same object; a new deliberate action receives a new object and new IDs.
internal sealed class FeatureSubmission
{
  public bool IsTask { get; }
  public bool SaveOnly { get; }
  public string CatalogRevision { get; }
  public string CoordinatorEpoch { get; }
  public string SelectionRevision { get; }
  public string Ids { get; }
  public string Mode { get; }
  public uint Duration { get; }
  public string TaskId { get; }
  public string TaskMode { get; }
  public string SelectCommand { get; } = Guid.NewGuid().ToString("D");
  public string ActionCommand { get; } = Guid.NewGuid().ToString("D");
  private string? appliedRevision;
  public FeatureSubmission(FeatureSelection selection, bool task, bool taskOnly = false, bool saveOnly = false)
  {
    if (!task && !selection.Ready) throw new InvalidOperationException("Choose a settings period");
    if (task && selection.TaskId != "conversation-close" && !taskOnly && (selection.Dirty || !selection.HasSelection))
      throw new InvalidOperationException("Save settings before requesting a task");
    SaveOnly = saveOnly && !task;
    IsTask = task; CatalogRevision = selection.Catalog.Revision;
    CoordinatorEpoch = selection.Catalog.CoordinatorEpoch;
    SelectionRevision = selection.Revision; Ids = selection.Ids;
    Mode = selection.Mode ?? "SESSION"; Duration = selection.Duration;
    TaskId = selection.TaskId; TaskMode = taskOnly ? "ONCE" : "SESSION";
  }
  public FeatureStatus? Execute(IFeatureBackend api, Func<bool> stopping)
  {
    if (IsTask) return api.Task(TaskId, TaskMode, CoordinatorEpoch, CatalogRevision, SelectionRevision, ActionCommand);
    if (appliedRevision is null)
    {
      var selected = api.Select(Ids, Mode, Duration, CoordinatorEpoch, CatalogRevision, SelectionRevision, SelectCommand);
      if (selected.CoordinatorEpoch != CoordinatorEpoch || selected.CatalogRevision != CatalogRevision)
        throw new NativeFailure(-116);
      if (Ids.Length == 0 || SaveOnly) return selected;
      appliedRevision = selected.Revision;
    }
    if (stopping()) return null;
    return api.Preapprove(CoordinatorEpoch, CatalogRevision, appliedRevision, ActionCommand);
  }
}

internal sealed class FeatureReview
{
  public FeatureSubmission Submission { get; }
  public IReadOnlyList<string> Pages { get; }
  public int Page { get; private set; }
  public int Reviewed { get; private set; } = 1;
  public bool CanApply => Reviewed == Pages.Count;
  public FeatureReview(FeatureSelection selection, bool korean, bool task = false, bool taskOnly = false, bool saveOnly = false)
  {
    Submission = new FeatureSubmission(selection, task, taskOnly, saveOnly);
    var pages = new List<string>();
    string period = task && taskOnly ? (korean ? "이번 한 번" : "One access only") :
        task || selection.Mode == "SESSION" ? (korean ? "이번 대화 동안" : "For this conversation") :
        (korean ? "승인 후 30분" : "For 30 minutes after approval");
    string[] taskFeatures = selection.TaskId switch
    {
      "calendar-summary" or "calendar-alternative" or "calendar-expanded" => new[] { "calendar.read" },
      "device-off" => new[] { "device.control" },
      "calendar-and-device" => new[] { "calendar.read", "device.control" },
      _ => Array.Empty<string>(),
    };
    if (task && (taskFeatures.Length == 0 || taskFeatures.Any(id => !selection.Catalog.Features.Any(feature => feature.Id == id))))
      throw new InvalidOperationException("Task has no review mapping");
    foreach (var feature in selection.Catalog.Features.Where(value =>
        task ? taskFeatures.Contains(value.Id, StringComparer.Ordinal) : selection.IsSelected(value.Id)))
    {
      foreach (var item in feature.Mappings.Where(value => value.TaskOnly == (task && selection.TaskId == "calendar-expanded")))
      {
        var mapping = item.Values;
        string description = korean ? feature.DescriptionKorean : feature.DescriptionEnglish;
        string detail = FeatureDisplay.Metadata(feature, mapping, period, korean);
        if (!task) detail += "\n" + description;
        if (task && selection.TaskId == "calendar-alternative")
          detail += korean ? "\n현재 대화에서 이미 보관 중인 달력 데이터만 사용합니다. 달력 데이터를 새로 취득하지 않습니다." :
              "\nUse only calendar data already held in this conversation. No new calendar acquisition.";
        if (task && taskOnly)
          detail += korean ? "\n이번 작업에만 적용하며 저장된 기능 설정은 변경하지 않습니다." :
              "\nThis task only; saved feature settings stay unchanged.";
        pages.AddRange(PromptSnapshot.Paginate(detail));
      }
    }
    if (pages.Count == 0 && (task || selection.HasSelection))
      throw new InvalidOperationException("Selected feature has no review mapping");
    if (pages.Count == 0) pages.Add(korean ? "모든 선택 기능을 해제합니다. 이후에는 별도 승인 없이 사용할 수 없습니다." :
        "Clear every selected feature. No later use is allowed without separate authorization.");
    if (pages.Count > 1024) throw new InvalidOperationException("Settings review is too large");
    Pages = pages.AsReadOnly();
  }
  public void Move(int offset)
  {
    if (offset is not (-1 or 1) || Page + offset < 0 || Page + offset >= Pages.Count)
      throw new InvalidOperationException("Invalid settings page");
    Page += offset; Reviewed = Math.Max(Reviewed, Page + 1);
  }
}
