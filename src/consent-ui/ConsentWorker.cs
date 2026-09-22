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

namespace ConsentUI;

internal sealed class ConsentWorker
{
  private readonly BlockingCollection<Action<IConsentBackend>> jobs = new(2);
  private readonly Thread thread;
  private readonly object lifetime = new();
  private bool disposed;
  private readonly Action<Exception> failed;
  private readonly Func<IConsentBackend> create;
  private int stopping;
  private bool decisionAttempted;
  private PromptSnapshot? latest;

  public ConsentWorker(Action<Exception> failed, Func<IConsentBackend>? create = null)
  {
    this.failed = failed;
    this.create = create ?? (() => new NativeApi());
    thread = new Thread(Run) { Name = "ConsentUI native owner", IsBackground = false };
    thread.Start();
  }

  public bool Fetch(string requestId, string locale, Action<PromptSnapshot> completed) =>
      Enqueue(api =>
      {
        var snapshot = api.Fetch(requestId, locale);
        latest = snapshot;
        if (Volatile.Read(ref stopping) == 0) completed(snapshot);
      });

  public bool Respond(PromptSnapshot snapshot, bool allow, Action<string> completed) =>
      Enqueue(api =>
      {
        // Never use a stale UI callback after a newer token was fetched.
        if (!ReferenceEquals(snapshot, latest) || (allow && !snapshot.CanApprove))
          throw new InvalidOperationException("Stale or unsupported UI choice");
        decisionAttempted = true;
        completed(api.Respond(snapshot, allow));
      });

  public void Stop()
  {
    lock (lifetime)
    {
      if (Interlocked.Exchange(ref stopping, 1) == 0) jobs.CompleteAdding();
    }
  }

  public bool Join(int milliseconds)
  {
    if (!thread.Join(milliseconds)) return false;
    lock (lifetime)
    {
      if (!disposed) { jobs.Dispose(); disposed = true; }
    }
    return true;
  }

  private bool Enqueue(Action<IConsentBackend> job)
  {
    lock (lifetime)
    {
      if (Volatile.Read(ref stopping) != 0) return false;
      return jobs.TryAdd(job);
    }
  }

  private void Run()
  {
    IConsentBackend? api = null;
    try
    {
      api = create();
      foreach (var job in jobs.GetConsumingEnumerable())
      {
        if (Volatile.Read(ref stopping) != 0) break;
        job(api);
      }
    }
    catch (Exception error)
    {
      // A racing approval can finish the request before a compact prompt is
      // produced. Do not fabricate a display error or send another decision.
      if (error is PromptFinished) { decisionAttempted = true; latest = null; }
      failed(error);
    }
    finally
    {
      Stop();
      if (api is not null)
      {
        if (!decisionAttempted && latest is not null)
        {
          try { api.Respond(latest, false); }
          catch (Exception error) { LogStatus("dismiss", error); }
        }
        try { api.Dispose(); }
        catch (Exception error) { LogStatus("destroy", error); }
      }
    }
  }

  internal static void LogStatus(string phase, Exception error) =>
      Console.Error.WriteLine($"ConsentUI {phase} status=" +
          (error is NativeFailure native ? native.Status.ToString() :
           error is UiFailure ui ? ui.Reason.ToString() : error.GetType().Name));
}
