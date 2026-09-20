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
    return new PromptSnapshot("request-1", locale, fields, new[] { ("May this app read?", body) });
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
    private int sequence;
    private void ThreadCheck() => Check(Environment.CurrentManagedThreadId == Owner, "Native owner thread changed");
    public PromptSnapshot Fetch(string id, string locale)
    {
      ThreadCheck(); Entered.Set(); Wait(Release);
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
    Model(); Review(); Actor();
  }
}
