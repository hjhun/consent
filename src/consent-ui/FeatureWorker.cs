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

internal sealed class FeatureWorker
{
  private readonly BlockingCollection<Action<IFeatureBackend>> jobs = new(2);
  private readonly object lifetime = new();
  private readonly Thread thread;
  private readonly Action<Exception> failed;
  private int stopping;
  private bool disposed;
  public FeatureWorker(Action<Exception> failed, Func<IFeatureBackend>? create = null)
  {
    this.failed = failed;
    thread = new Thread(() => Run(create ?? (() => new FeatureApi())))
        { Name = "ConsentUI feature owner", IsBackground = false };
    thread.Start();
  }
  public bool Catalog(Action<FeatureCatalog> completed) => Enqueue(api => completed(api.Catalog()));
  public bool Status(Action<FeatureStatus> completed) => Enqueue(api => completed(api.Status()));
  public bool Submit(FeatureSubmission submission, Action<FeatureStatus> completed) => Enqueue(api =>
  {
    var status = submission.Execute(api, () => Volatile.Read(ref stopping) != 0);
    if (status is not null) completed(status);
  });
  public void Stop()
  { lock (lifetime) { if (Interlocked.Exchange(ref stopping, 1) == 0) jobs.CompleteAdding(); } }
  public bool Join(int milliseconds)
  {
    if (!thread.Join(milliseconds)) return false;
    lock (lifetime) { if (!disposed) { jobs.Dispose(); disposed = true; } }
    return true;
  }
  private bool Enqueue(Action<IFeatureBackend> job)
  {
    lock (lifetime) { return Volatile.Read(ref stopping) == 0 && jobs.TryAdd(job); }
  }
  private void Run(Func<IFeatureBackend> create)
  {
    try
    {
      var api = create();
      foreach (var job in jobs.GetConsumingEnumerable())
      {
        if (Volatile.Read(ref stopping) != 0) break;
        try { job(api); }
        catch (Exception error) { failed(error); }
      }
    }
    catch (Exception error) { failed(error); }
    finally { Stop(); }
  }
}
