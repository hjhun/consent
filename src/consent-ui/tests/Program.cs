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
using System.Collections.Concurrent;
using System.Globalization;
using System.Text.Json;
using ConsentUI;

static class Program
{
  private static void Check(bool condition, string message)
  {
    if (!condition) throw new InvalidOperationException(message);
  }
  private static void Wait(ManualResetEventSlim signal) => Check(signal.Wait(3000), "Test timed out");
  private static void Reject(Action action)
  {
    try { action(); }
    catch (InvalidOperationException) { return; }
    throw new Exception("Expected validation rejection");
  }
  private static PromptSnapshot Snapshot(string token = "token-1", string body = "Read sink{name}%<tag>",
      string locale = "en-US", Action<Dictionary<string, string>>? mutate = null)
  {
    var fields = new Dictionary<string, string>
    {
      ["request_id"] = "request-1", ["prompt_token"] = token, ["count"] = "1",
      ["template_version"] = "1", ["locale"] = locale, ["epoch"] = "epoch-1",
      ["session"] = "session-1", ["generation"] = "1", ["subject"] = "subject-1",
      ["profile"] = "profile-1", ["r0.definition"] = "internal-definition-id",
      ["r0.title"] = "May this app read?", ["r0.body"] = body,
      ["r0.policy_version"] = "1", ["r0.text_revision"] = "1",
      ["r0.locale"] = locale, ["r0.modes"] = "ONCE,SESSION",
    };
    mutate?.Invoke(fields);
    return new PromptSnapshot("request-1", locale, fields, new[] { ("May this app read?", body) },
        fields.ContainsKey("approval_version") ? new FeatureCatalog(CatalogJson()) : null);
  }

  private static PromptSnapshot Versioned(string mode = "SESSION", Action<Dictionary<string, string>>? mutate = null, string locale = "en-US") =>
      Snapshot(body: new string('A', 700), locale: locale, mutate: fields =>
      {
        fields["approval_version"] = "1";
        fields["request_kind"] = "PREAPPROVAL";
        fields["selection_id"] = "selection-1";
        fields["selection_revision"] = "1";
        fields["selection_digest"] = new string('a', 64);
        fields["grant_mode"] = mode;
        fields["total_count"] = "3";
        fields["r0.original_index"] = "2";
        fields["r0.feature_id"] = "calendar.read";
        fields["r0.feature_revision"] = "1";
        fields["r0.provider_package"] = "org.tizen.provider";
        fields["r0.provider_app"] = "org.tizen.provider.app";
        fields["r0.modes"] = "ONCE,SESSION,TIMED";
        fields["r0.operation"] = "read"; fields["r0.scope"] = "calendar.default.next7days";
        fields["r0.purpose"] = "conversation-summary"; fields["r0.recipient"] = "local-conversation";
        fields["r0.holder"] = "mock-holder"; fields["r0.retention_ms"] = "1800000";
        if (mode == "TIMED") fields["duration_ms"] = "1800000";
        mutate?.Invoke(fields);
      });

  private static void ApprovalBinding()
  {
    var session = Versioned();
    Check(session.CanApprove && !session.CanAllowOnce && session.TotalCount == 3, "SESSION compact prompt accepted");
    Check(session.PeriodLabel(false) == "For this conversation", "Session period is explicit");
    var fields = session.ResponseFields(true);
    foreach (string key in new[] { "approval_version", "request_kind", "selection_id", "selection_revision",
        "selection_digest", "grant_mode", "count", "total_count", "r0.original_index", "r0.feature_id",
        "r0.feature_revision", "r0.provider_package", "r0.provider_app", "session", "generation" })
      Check(fields[key] == session.Fields[key], "Exact approval response binding");
    Check(!fields.ContainsKey("r0.body") && !fields.ContainsKey("r0.title"), "No independent display content in response");
    var timed = Versioned("TIMED");
    Check(timed.PeriodLabel(false) == "For 30 minutes after approval" && timed.ResponseFields(true)["duration_ms"] == "1800000",
        "TIMED duration preserved exactly");
    Check(timed.ResponseFields(false)["decision"] == "DENIED", "Deny preserves fixed context");
    foreach (string key in new[] { "selection_id", "selection_revision", "selection_digest", "r0.feature_revision",
        "r0.original_index", "r0.provider_package", "r0.provider_app" })
      Reject(() => Versioned(mutate: value => value.Remove(key)));
    Reject(() => Versioned(mutate: value => value["approval_version"] = "2"));
    Reject(() => Versioned(mutate: value => value["selection_revision"] = "01"));
    Reject(() => Versioned(mutate: value => value["selection_digest"] = new string('Z', 64)));
    Reject(() => Versioned(mutate: value => value["r0.original_index"] = "3"));
    Reject(() => Versioned(mutate: value => value["session"] = ""));
    Reject(() => Versioned(mutate: value => value["duration_ms"] = "1800000"));
    Reject(() => Versioned("TIMED", value => value["duration_ms"] = "3600001"));
    Reject(() => Versioned("PERSISTENT"));
    Check(!Versioned(mutate: value => value["r0.modes"] = "ONCE").CanApprove, "No policy mode expansion");
    foreach (string key in new[] { "selection_id", "selection_revision" })
      Check(!session.SameContent(Versioned(mutate: value => value[key] = "2")), "Changed mapping resets review");
    foreach (string key in new[] { "r0.feature_id", "r0.feature_revision", "r0.provider_package", "r0.provider_app",
        "r0.operation", "r0.scope", "r0.purpose", "r0.recipient", "r0.holder", "r0.retention_ms" })
      Reject(() => Versioned(mutate: value => value[key] = "unrecognized"));
    string visible = string.Join("", session.Pages).Replace("\n", "", StringComparison.Ordinal);
    foreach (string required in new[] { "Calendar access", "Calendar provider", "Operation: Read calendar", "Scope: Next 7 days",
        "Purpose: Conversation summary", "Recipient: Current conversation on this device", "Access approval period: For this conversation",
        "Acquired-data retention: Up to 30 minutes; ends earlier when the conversation closes or access is revoked." })
      Check(visible.Contains(required, StringComparison.Ordinal), "Exact tuple/access/retention visible independently of generic template");
    string korean = string.Concat(Versioned("TIMED", locale: "ko-KR").Pages).Replace("\n", "", StringComparison.Ordinal);
    foreach (string required in new[] { "향후 7일", "대화 요약", "이 기기의 현재 대화", "접근 승인 기간: 승인 후 30분",
        "취득 데이터 보관: 최대 30분", "대화 종료나 접근 승인 취소 시 더 일찍 종료" })
      Check(korean.Contains(required, StringComparison.Ordinal), "Korean exact human display and distinct periods");
    foreach (string raw in new[] { "calendar.default.next7days", "conversation-summary", "local-conversation" })
      Check(!visible.Contains(raw, StringComparison.Ordinal) && !korean.Contains(raw, StringComparison.Ordinal), "No raw tuple codes shown");
    Check(string.Concat(Versioned(mutate: value => value["r0.scope"] = "calendar.default.next30days").Pages)
        .Replace("\n", "", StringComparison.Ordinal).Contains("Next 30 days", StringComparison.Ordinal), "Expanded exact mapping has explicit scope");
    foreach (string language in new[] { "en-US", "ko-KR" })
    {
      var device = Versioned(mutate: value =>
      {
        value["r0.feature_id"] = "device.control"; value["r0.definition"] = "device-definition-id";
        value["r0.operation"] = "control"; value["r0.scope"] = "device.study-lamp.action.off.impact.study-dark";
        value["r0.purpose"] = "requested-device-control"; value["r0.recipient"] = "local-device";
        value["r0.holder"] = ""; value["r0.retention_ms"] = "0";
      }, locale: language);
      string display = string.Concat(device.Pages).Replace("\n", "", StringComparison.Ordinal);
      Check(display.Contains(language == "ko-KR" ? "서재 조명을 끔(서재가 어두워짐)" : "Turn off study lamp (study becomes dark)", StringComparison.Ordinal) &&
          display.Contains(language == "ko-KR" ? "결과 보관 없음" : "No result retention", StringComparison.Ordinal) &&
          !display.Contains("device.study-lamp.action.off.impact.study-dark", StringComparison.Ordinal), "Device target, impact and zero retention are human readable");
    }
    var review = new PromptReview(); review.Install(session);
    while (review.Page + 1 < session.Pages.Count) review.Move(1);
    Check(review.CanAllow, "SESSION every-page review allows bound mode");
    review.Install(Versioned("TIMED"));
    Check(!review.CanAllow && review.Page == 0, "Period changes reset review");
    Console.WriteLine("PASS approval v1 compact rows, feature/provider binding, fixed periods and exact response echo");
  }

  private static string CatalogJson(string epoch = "broker-1") => JsonSerializer.Serialize(new
  {
    schema = 1, catalog_revision = new string('b', 64), coordinator_epoch = epoch, selection_revision = "1",
    features = new[] {
      new { id = "calendar.read", revision = "1", title_en = "Calendar access", title_ko = "일정 접근",
        description_en = "Optional calendar feature", description_ko = "선택 일정 기능", provider_label = "Calendar provider",
        provider_package = "org.tizen.provider", provider_app = "org.tizen.provider.app",
        mappings = new[] {
          new { definition = "internal-definition-id", policy_version = "1", operation = "read", scope = "calendar.default.next7days",
            purpose = "conversation-summary", recipient = "local-conversation", holder = "mock-holder", retention_ms = "1800000", task_only = false },
          new { definition = "internal-definition-id", policy_version = "1", operation = "read", scope = "calendar.default.next30days",
            purpose = "conversation-summary", recipient = "local-conversation", holder = "mock-holder", retention_ms = "1800000", task_only = true },
        } },
      new { id = "device.control", revision = "1", title_en = "Device control", title_ko = "기기 제어",
        description_en = "Optional device feature", description_ko = "선택 기기 기능", provider_label = "Device provider",
        provider_package = "org.tizen.provider", provider_app = "org.tizen.provider.app",
        mappings = new[] { new { definition = "device-definition-id", policy_version = "1", operation = "control", scope = "device.study-lamp.action.off.impact.study-dark",
          purpose = "requested-device-control", recipient = "local-device", holder = "", retention_ms = "0", task_only = false } } },
    },
    tasks = new[] { "calendar-summary", "device-off", "calendar-and-device", "calendar-alternative", "calendar-expanded", "conversation-close" },
    presets = new[] { "SESSION", "TIMED" },
  });
  private static string StatusJson(string revision = "1", string decision = "IDLE", string epoch = "broker-1") => JsonSerializer.Serialize(new
  {
    schema = 1, decision, request_id = decision == "PENDING" ? "request-1" : "",
    catalog_revision = new string('b', 64), coordinator_epoch = epoch,
    selection_id = "selection-1", selection_revision = revision, selection_digest = new string('a', 64),
    grant_mode = "SESSION", duration_ms = "0", settings_grant_mode = "SESSION", settings_duration_ms = "0",
    selected = new[] { "calendar.read" }, session = "session-1", generation = "1", reason = "", cleanup_state = "", cleanup_pending = "", execution_pending = "0",
  });
  private sealed class Features : IFeatureBackend
  {
    public int Owner;
    public string Epoch = "broker-1";
    public int Actions;
    public bool LoseReply;
    public readonly List<(string Method, string Value, string Revision, string Command)> Calls = new();
    public readonly ManualResetEventSlim Entered = new();
    public readonly ManualResetEventSlim Release = new(true);
    public bool Conflict;
    private readonly Dictionary<string, FeatureStatus> ledger = new();
    private void Record(string method, string value = "", string revision = "", string command = "")
    {
      Check(Environment.CurrentManagedThreadId == Owner, "Feature owner thread changed");
      Calls.Add((method, value, revision, command));
    }
    private FeatureStatus Mutation(string method, string value, string epoch, string catalog, string revision, string command)
    {
      Record(method, value, revision, command);
      Check(catalog == new string('b', 64), "Every mutation binds catalog digest");
      if (epoch != Epoch || Conflict) throw new NativeFailure(-116);
      if (ledger.TryGetValue(command, out var old)) return old;
      Entered.Set(); Wait(Release);
      var status = new FeatureStatus(StatusJson("2", method == "select" ? "IDLE" : "PENDING", Epoch));
      ledger.Add(command, status);
      if (method != "select") ++Actions;
      if (LoseReply) { LoseReply = false; throw new NativeFailure(-110); }
      return status;
    }
    public FeatureCatalog Catalog() { Record("catalog"); return new(CatalogJson(Epoch)); }
    public FeatureStatus Status() { Record("status"); return new(StatusJson(epoch: Epoch)); }
    public FeatureStatus Select(string ids, string mode, uint duration, string epoch, string catalog, string revision, string command) =>
        Mutation("select", ids + ":" + mode + ":" + duration, epoch, catalog, revision, command);
    public FeatureStatus Preapprove(string epoch, string catalog, string revision, string command) =>
        Mutation("preapprove", "", epoch, catalog, revision, command);
    public FeatureStatus Task(string task, string mode, string epoch, string catalog, string revision, string command) =>
        Mutation("task", task + ":" + mode, epoch, catalog, revision, command);
  }
  private static void Settings()
  {
    var catalog = new FeatureCatalog(CatalogJson());
    var selection = new FeatureSelection(catalog);
    Check(!selection.Ready && selection.Ids == "" && selection.Mode is null, "Settings default unselected and no implied period");
    selection.Toggle("calendar.read");
    Check(!selection.Ready, "Period must be explicitly selected");
    selection.NextMode();
    Check(selection.Ready && selection.Mode == "SESSION" && selection.Duration == 0, "SESSION preset exact");
    selection.NextMode();
    Check(selection.Mode == "TIMED" && selection.Duration == 1800000, "Only 30-minute TIMED preset");
    selection.NextMode(); Check(selection.Mode == "SESSION", "No ONCE settings preset");
    Reject(() => selection.Toggle("calendar.read,device.control"));
    Reject(() => new FeatureCatalog(CatalogJson().Replace("calendar-summary", "run-shell", StringComparison.Ordinal)));
    Reject(() => new FeatureCatalog(CatalogJson().Replace("\"SESSION\"", "\"ONCE\"", StringComparison.Ordinal)));
    Reject(() => new FeatureCatalog(CatalogJson().Replace("\"schema\":1", "\"schema\":1,\"schema\":1", StringComparison.Ordinal)));
    var pending = new FeatureStatus(StatusJson("1", "PENDING"));
    Check(pending.Matches(Versioned()), "Settings→popup exact selection context");
    Check(!pending.Matches(Versioned(mutate: f => f["selection_revision"] = "2")), "Changed selection rejected before display");
    var unknownCatalog = new FeatureCatalog(CatalogJson().Replace("calendar.default.next7days", "calendar.default.unknown", StringComparison.Ordinal));
    var unknownSelection = new FeatureSelection(unknownCatalog); unknownSelection.Toggle("calendar.read"); unknownSelection.NextMode();
    Reject(() => new FeatureReview(unknownSelection, false));
    var review = new FeatureReview(selection, false);
    string detail = string.Join("", review.Pages).Replace("\n", "", StringComparison.Ordinal);
    foreach (string field in new[] { "Calendar access", "Calendar provider", "Optional calendar feature", "Operation: Read calendar",
        "Scope: Next 7 days", "Purpose: Conversation summary", "Recipient: Current conversation on this device", "Access approval period: For this conversation",
        "Acquired-data retention: Up to 30 minutes; ends earlier when the conversation closes or access is revoked." })
      Check(detail.Contains(field, StringComparison.Ordinal), "Settings review displays complete exact tuple before saving");
    Check(!detail.Contains("Next 30 days", StringComparison.Ordinal), "Task-only mapping never becomes a saved setting");
    string koreanSettings = string.Concat(new FeatureReview(selection, true).Pages).Replace("\n", "", StringComparison.Ordinal);
    foreach (string expected in new[] { "향후 7일", "대화 요약", "이 기기의 현재 대화", "접근 승인 기간: 이번 대화 동안", "취득 데이터 보관: 최대 30분" })
      Check(koreanSettings.Contains(expected, StringComparison.Ordinal), "Settings Korean human tuple display");
    Check(!detail.Contains("calendar.default", StringComparison.Ordinal) && !koreanSettings.Contains("calendar.default", StringComparison.Ordinal),
        "Settings does not display raw scope codes");
    Check(!review.CanApply, "Settings requires every page");
    while (review.Page + 1 < review.Pages.Count) review.Move(1);
    Check(review.CanApply, "Settings full review permits mutation");
    var backend = new Features(); backend.Release.Reset();
    using var done = new ManualResetEventSlim();
    Exception? failure = null;
    var worker = new FeatureWorker(error => { failure = error; done.Set(); }, () =>
    { backend.Owner = Environment.CurrentManagedThreadId; return backend; });
    Check(worker.Submit(review.Submission, _ => done.Set()), "Settings click admitted");
    Wait(backend.Entered);
    selection.Toggle("device.control"); selection.NextMode();
    backend.Release.Set(); Wait(done); worker.Stop(); Check(worker.Join(3000), "Feature worker stop");
    Check(failure is null && backend.Calls.Count == 2 && backend.Calls[0].Value == "calendar.read:SESSION:0" &&
        backend.Calls[0].Revision == "1" && backend.Calls[1].Method == "preapprove" && backend.Calls[1].Revision == "2" &&
        backend.Calls[0].Command != backend.Calls[1].Command,
        "Immutable click snapshot and CAS response revision, distinct stable command IDs");
    var cleaned = StatusJson(decision: "ALLOWED").Replace("\"cleanup_state\":\"\"", "\"cleanup_state\":\"CLOSED\"", StringComparison.Ordinal)
        .Replace("\"cleanup_pending\":\"\"", "\"cleanup_pending\":\"0\"", StringComparison.Ordinal);
    Check(new FeatureStatus(cleaned).CleanupComplete && !new FeatureStatus(StatusJson(decision: "ALLOWED")).CleanupComplete,
        "Approval alone never claims holder cleanup");
    selection.Restore(pending);
    Check(!selection.Dirty && selection.Mode == "SESSION" && selection.Ids == "calendar.read", "Relaunch restores saved settings read-only");
    var taskReview = new FeatureReview(selection, false, true);
    Check(taskReview.Submission.IsTask && !taskReview.CanApply, "Task requires complete review even when grants already exist");
    string taskText = string.Concat(taskReview.Pages).Replace("\n", "", StringComparison.Ordinal);
    foreach (string required in new[] { "Calendar provider", "Scope: Next 7 days", "Purpose: Conversation summary",
        "Recipient: Current conversation on this device", "Access approval period: For this conversation" })
      Check(taskText.Contains(required, StringComparison.Ordinal), "Task choice shows exact provider, tuple and SESSION period");
    while (taskReview.Page + 1 < taskReview.Pages.Count) taskReview.Move(1);
    Check(taskReview.CanApply, "Reviewed task can be requested");
    backend = new Features { Owner = Environment.CurrentManagedThreadId };
    taskReview.Submission.Execute(backend, () => false);
    Check(backend.Calls.Count == 1 && backend.Calls[0].Method == "task" && backend.Calls[0].Value == "calendar-summary:SESSION",
        "Saved-feature task never rewrites settings or refreshes their period");
    var expandedSelection = new FeatureSelection(catalog); expandedSelection.Restore(pending);
    for (int i = 0; i < 4; ++i) expandedSelection.NextTask();
    var expandedReview = new FeatureReview(expandedSelection, false, true, true);
    string expandedText = string.Concat(expandedReview.Pages).Replace("\n", "", StringComparison.Ordinal);
    Check(expandedText.Contains("Scope: Next 30 days", StringComparison.Ordinal) && !expandedText.Contains("Scope: Next 7 days", StringComparison.Ordinal) &&
        !expandedText.Contains("Optional calendar feature", StringComparison.Ordinal) &&
        expandedText.Contains("Access approval period: One access only", StringComparison.Ordinal), "Task-only review uses expanded exact tuple and ONCE period");
    backend = new Features { Owner = Environment.CurrentManagedThreadId };
    expandedReview.Submission.Execute(backend, () => false);
    Check(backend.Calls.Single().Value == "calendar-expanded:ONCE" && expandedSelection.Ids == "calendar.read" && expandedSelection.Mode == "SESSION",
        "Expanded task review executes without selecting or changing saved settings");
    var executing = new FeatureStatus(StatusJson(decision: "PENDING").Replace("\"execution_pending\":\"0\"", "\"execution_pending\":\"1\"", StringComparison.Ordinal));
    Check(executing.ExecutionPending, "Execution remains distinguishable from approval pending");
    var draft = new FeatureSelection(catalog); draft.Restore(pending); draft.Toggle("calendar.read");
    draft.Reconcile(executing);
    Check(draft.Dirty && draft.Ids == "", "Readonly execution poll preserves unsaved clear selection");
    draft.Toggle("device.control"); draft.Reconcile(executing);
    Check(draft.Dirty && draft.Ids == "device.control", "Readonly execution poll preserves unsaved checks");
    draft.Reconcile(pending, true);
    Check(draft.Dirty && draft.Ids == "calendar.read", "Explicit stale reconciliation requires new review");
    var editDuringExecution = new FeatureReview(selection, false, saveOnly: true);
    backend = new Features { Owner = Environment.CurrentManagedThreadId };
    editDuringExecution.Submission.Execute(backend, () => false);
    Check(backend.Calls.Count == 1 && backend.Calls.Single().Method == "select", "Editing executing selection saves only, never queues another preapproval/task");
    selection.Toggle("calendar.read");
    Reject(() => new FeatureSubmission(selection, true));
    backend = new Features { Owner = Environment.CurrentManagedThreadId };
    new FeatureSubmission(selection, true, true).Execute(backend, () => false);
    Check(backend.Calls.Single().Value == "calendar-summary:ONCE" && selection.Ids == "", "Explicit task-only ONCE never selects settings");
    backend = new Features { Owner = Environment.CurrentManagedThreadId };
    new FeatureSubmission(selection, false).Execute(backend, () => false);
    Check(backend.Calls.Count == 1 && backend.Calls[0].Method == "select" && backend.Calls[0].Value == ":SESSION:0",
        "Empty selection clears everything without starting a preapproval");
    selection.Restore(pending);
    backend = new Features { Owner = Environment.CurrentManagedThreadId, LoseReply = true };
    var retry = new FeatureSubmission(selection, true);
    try { retry.Execute(backend, () => false); throw new Exception("Expected unknown reply"); }
    catch (NativeFailure error) { Check(error.Status == -110, "Unknown transport outcome"); }
    retry.Execute(backend, () => false);
    Check(backend.Actions == 1 && backend.Calls.Count == 2 && backend.Calls[0].Command == backend.Calls[1].Command,
        "Accepted task with lost reply explicitly retries same ID exactly once");
    backend.Epoch = "broker-2";
    try { retry.Execute(backend, () => false); throw new Exception("Expected stale epoch"); }
    catch (NativeFailure error) { Check(error.Status == -116, "Restart epoch rejects old command"); }
    Check(backend.Actions == 1, "Restart never replays old accepted action");
    Reject(() => selection.Restore(new FeatureStatus(StatusJson(epoch: "broker-2"))));
    backend = new Features { Owner = Environment.CurrentManagedThreadId, LoseReply = true };
    retry = new FeatureSubmission(selection, false);
    try { retry.Execute(backend, () => false); throw new Exception("Expected unknown select reply"); }
    catch (NativeFailure error) { Check(error.Status == -110, "Unknown select result"); }
    retry.Execute(backend, () => false);
    Check(backend.Calls.Count == 3 && backend.Calls[0].Command == backend.Calls[1].Command && backend.Actions == 1,
        "Uncertain select retries same command then preapproves once");
    backend = new Features { Conflict = true }; done.Reset(); failure = null;
    worker = new FeatureWorker(error => { failure = error; done.Set(); }, () =>
    { backend.Owner = Environment.CurrentManagedThreadId; return backend; });
    worker.Submit(new FeatureSubmission(selection, true), _ => done.Set()); Wait(done);
    worker.Stop(); Check(worker.Join(3000), "Conflict worker stop");
    Check(failure is NativeFailure { Status: -116 } && backend.Calls.Count == 1, "Stale mutation never auto-retries");
    backend = new Features(); backend.Release.Reset(); done.Reset();
    worker = new FeatureWorker(_ => done.Set(), () =>
    { backend.Owner = Environment.CurrentManagedThreadId; return backend; });
    worker.Submit(new FeatureSubmission(selection, false), _ => done.Set()); Wait(backend.Entered);
    worker.Stop(); backend.Release.Set(); Check(worker.Join(3000), "Settings close during select");
    Check(backend.Calls.Count == 1, "Close before select completion never preapproves");
    var launch = new LaunchGate();
    Check(launch.ReadInitial(() => new LaunchRequest("http://tizen.org/appcontrol/operation/default", null, "en-US"))?.Settings == true,
        "Default launcher opens settings only");
    Check(launch.ReadInitial(() => throw new Exception("Do not inspect active launch")) is null,
        "Active settings ignores third-party launches");
    Console.WriteLine("PASS settings review, saved selection, separate TASK/ONCE, empty clear, stable retry, restart epoch, CAS and close race");
  }

  private static void Model()
  {
    var original = Snapshot();
    Check(original.CanAllowOnce, "ONCE support");
    string visible = string.Join("", original.Pages).Replace("\n", "", StringComparison.Ordinal);
    Check(visible.Contains("sink{name}%<tag>", StringComparison.Ordinal), "Text must remain literal");
    Check(!visible.Contains("internal-definition-id", StringComparison.Ordinal), "Internal codes must not be displayed");
    Check(original.SameContent(Snapshot("token-2")), "Token refresh preserves review");
    foreach (string key in new[] { "epoch", "session", "generation", "subject", "profile", "r0.policy_version", "r0.text_revision" })
      Check(!original.SameContent(Snapshot(mutate: f => f[key] = "2")), "Changed binding resets review");
    Check(!original.SameContent(Snapshot(locale: "ko-KR")), "Locale resets review");
    Check(!Snapshot(mutate: f => f["r0.modes"] = "PERSISTENT").CanAllowOnce, "No implicit persistent approval");
    Check(!Snapshot(mutate: f => f["r0.modes"] = "ONCEFUL").CanAllowOnce, "Exact mode token");
    Reject(() => Snapshot(mutate: f => f["locale"] = "ko-KR"));
    Reject(() => Snapshot(mutate: f => f.Remove("r0.policy_version")));
    Reject(() => Snapshot(mutate: f => f["request_id"] = "other"));
    Reject(() => Snapshot(body: "Invisible\u202econtrol"));
    var pages = PromptSnapshot.Paginate(string.Concat(Enumerable.Repeat("한글🙂ab", 400)));
    Check(pages.Count > 2, "Large text must paginate");
    foreach (string page in pages)
    {
      Check(page.Split('\n').Length <= 6, "Bounded page height");
      foreach (string line in page.Split('\n'))
        Check(new StringInfo(line).LengthInTextElements <= 26, "Bounded page width");
    }
    foreach (float invalid in new[] { float.NaN, float.PositiveInfinity, -1f, 291f })
      Check(!PopupTextMetrics.HeightFits(invalid), "Nonfinite/overflow height rejected");
    Check(!PopupTextMetrics.HeightFits(0) && PopupTextMetrics.HeightFits(290), "Positive constrained height bound");
    Check(!PopupTextMetrics.GlyphFits(753) && !PopupTextMetrics.GlyphFits(float.NaN) &&
        PopupTextMetrics.GlyphFits(752), "Individual glyph width bound");
    foreach (string text in new[] { new string('W', 8192), new string('힣', 8192), "verylongunbrokenrecipient{name}%<tag>" + new string('X', 512) })
    {
      var allPages = PromptSnapshot.Paginate(text);
      Check(string.Concat(allPages).Replace("\n", "", StringComparison.Ordinal) == text,
          "Wide/long content and last page preserved without elision");
    }
    Console.WriteLine("PASS prompt binding, plain text, exact modes, full pagination and constrained metric guards");
  }

  private static void Review()
  {
    var review = new PromptReview();
    var prompt = Snapshot(body: new string('A', 700));
    review.Install(prompt);
    Check(!review.CanAllow, "Cannot allow before all pages");
    while (review.Page + 1 < prompt.Pages.Count) review.Move(1);
    Check(review.CanAllow, "Full review allows ONCE");
    review.Install(Snapshot("token-2", new string('A', 700)));
    Check(review.CanAllow && review.Snapshot!.Token == "token-2", "Refresh retains review and replaces token");
    review.Install(Snapshot("token-3", new string('A', 700), mutate: f => f["r0.policy_version"] = "2"));
    Check(!review.CanAllow && review.Page == 0, "Policy change requires fresh review");
    Reject(() => review.Move(10));
    review.Install(Snapshot(mutate: f => f["r0.modes"] = "TIMED"));
    Check(!review.CanAllow, "Review cannot enable non-ONCE mode");
    var launches = new LaunchGate();
    Check(launches.ReadInitial(() => new LaunchRequest("http://tizen.org/appcontrol/operation/view", "request-1", "en-US")) is not null,
        "Valid initial launch accepted");
    Check(launches.ReadInitial(() => throw new Exception("Must not parse third-party relaunch")) is null,
        "Active request ignores every later app-control before parsing");
    Reject(() => new LaunchGate().ReadInitial(() => new LaunchRequest("default", null, "en-US")));
    Console.WriteLine("PASS every-page review, refreshed-token reuse, policy reset and unrelated relaunch isolation");
  }

  private sealed class Backend : IConsentBackend
  {
    public readonly ConcurrentQueue<(PromptSnapshot Snapshot, bool Allow)> Choices = new();
    public readonly ManualResetEventSlim Entered = new();
    public readonly ManualResetEventSlim Release = new(true);
    public int Owner;
    public bool Disposed;
    public bool ResponseFailure;
    public bool TerminalFetch;
    private int sequence;
    private void ThreadCheck() => Check(Environment.CurrentManagedThreadId == Owner, "Native owner thread changed");
    public PromptSnapshot Fetch(string id, string locale)
    {
      ThreadCheck(); Entered.Set(); Wait(Release);
      if (TerminalFetch) throw new PromptFinished("ALLOWED");
      return Snapshot($"token-{++sequence}");
    }
    public string Respond(PromptSnapshot snapshot, bool allow)
    {
      ThreadCheck(); Choices.Enqueue((snapshot, allow));
      if (ResponseFailure) throw new NativeFailure(-1234);
      return allow ? "ALLOWED" : "DENIED";
    }
    public void Dispose() { ThreadCheck(); Disposed = true; }
  }

  private static ConsentWorker Worker(Backend backend, Action<Exception> failed) => new(failed, () =>
  { backend.Owner = Environment.CurrentManagedThreadId; return backend; });

  private static void Actor()
  {
    var backend = new Backend();
    using var ready = new ManualResetEventSlim();
    using var responded = new ManualResetEventSlim();
    Exception? failure = null;
    var worker = Worker(backend, error => { failure = error; responded.Set(); });
    PromptSnapshot? prompt = null;
    Check(worker.Fetch("request-1", "en-US", item => { prompt = item; ready.Set(); }), "Fetch admitted");
    Wait(ready);
    Check(worker.Respond(prompt!, true, _ => responded.Set()), "Choice admitted");
    Wait(responded); worker.Stop(); Check(worker.Join(3000), "Worker closed");
    Check(failure is null && backend.Disposed && backend.Choices.Count == 1 && backend.Choices.Single().Allow,
        "Exactly one deliberate choice and same-thread dispose");

    backend = new Backend(); backend.Release.Reset();
    bool callback = false;
    worker = Worker(backend, error => failure = error);
    Check(worker.Fetch("request-1", "en-US", _ => callback = true), "Fetch admitted");
    Wait(backend.Entered);
    Check(worker.Fetch("request-1", "en-US", _ => { }), "Queue first slot");
    Check(worker.Fetch("request-1", "en-US", _ => { }), "Queue second slot");
    Check(!worker.Fetch("request-1", "en-US", _ => { }), "Bounded queue rejects overflow");
    worker.Stop(); backend.Release.Set(); Check(worker.Join(3000), "Fetch close bounded");
    Check(!callback && backend.Choices.Count == 1 && !backend.Choices.Single().Allow,
        "Close during fetch denies fetched token and suppresses callback");

    backend = new Backend(); ready.Reset(); responded.Reset(); failure = null;
    worker = Worker(backend, error => { failure = error; responded.Set(); });
    worker.Fetch("request-1", "en-US", item => { prompt = item; ready.Set(); }); Wait(ready);
    var stale = prompt!; ready.Reset();
    worker.Fetch("request-1", "en-US", _ => ready.Set()); Wait(ready);
    worker.Respond(stale, true, _ => responded.Set()); Wait(responded);
    Check(worker.Join(3000), "Stale callback stops actor");
    Check(failure is InvalidOperationException && backend.Choices.Count == 1 &&
        !backend.Choices.Single().Allow && backend.Choices.Single().Snapshot.Token == "token-2",
        "Stale UI token never approves, newest token denied");

    backend = new Backend { ResponseFailure = true }; ready.Reset(); responded.Reset();
    worker = Worker(backend, _ => responded.Set());
    worker.Fetch("request-1", "en-US", item => { prompt = item; ready.Set(); }); Wait(ready);
    worker.Respond(prompt!, true, _ => responded.Set()); Wait(responded);
    Check(worker.Join(3000), "Response failure stops actor");
    Check(backend.Choices.Count == 1 && backend.Choices.Single().Allow,
        "Uncertain submitted decision is not retried or replaced");
    backend = new Backend(); ready.Reset(); responded.Reset(); failure = null;
    worker = Worker(backend, error => { failure = error; responded.Set(); });
    worker.Fetch("request-1", "en-US", _ => ready.Set()); Wait(ready);
    backend.TerminalFetch = true;
    worker.Fetch("request-1", "en-US", _ => throw new Exception("No terminal prompt")); Wait(responded);
    Check(worker.Join(3000), "Terminal prompt completion bounded");
    Check(failure is PromptFinished { Decision: "ALLOWED" } && backend.Choices.Count == 0,
        "Terminal prompt completion clears prior token and sends no DENIED response");

    for (int i = 0; i < 100; ++i)
    {
      var racing = Worker(new Backend(), _ => { });
      var first = Task.Run(() => racing.Stop());
      var second = Task.Run(() => { racing.Stop(); Check(racing.Join(3000), "Concurrent join bounded"); });
      Task.WaitAll(first, second);
      racing.Stop();
      Check(racing.Join(3000), "Repeated join is safe");
      Check(!racing.Fetch("request-1", "en-US", _ => { }), "Disposed worker refuses work");
    }
    Console.WriteLine("PASS native owner thread, bounded queue, close/fetch race, stale token, uncertain response and concurrent stop/join");
  }

  public static void Main()
  {
    Model(); ApprovalBinding(); Settings(); Review(); Actor();
  }
}
