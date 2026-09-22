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
using Tizen.Applications;
using Tizen.NUI;

namespace ConsentUI;

internal sealed class NegativeApplication : NUIApplication
{
  private Thread? thread;
  private Tizen.NUI.Timer? timer;
  private int complete;

  protected override void OnCreate()
  {
    base.OnCreate();
    string output = System.IO.Path.Combine(DirectoryInfo.Data, "negative-result.txt");
    // Leave a short inspection window for the external test to compare the
    // real UID, shared managed launcher, and distinct package SMACK label.
    timer = new Tizen.NUI.Timer(10000);
    timer.Tick += (_, _) =>
    {
      if (Volatile.Read(ref complete) == 0) return true;
      Exit();
      return false;
    };
    timer.Start();
    thread = new Thread(() => Probe(output)) { Name = "ConsentUI negative native owner" };
    thread.Start();
  }

  private void Probe(string output)
  {
    string result;
    string stage = "create";
    try
    {
      using var api = new NativeApi();
      stage = "get_prompt";
      api.Fetch("negative-identity-probe", "en-US");
      result = "FAIL unexpected prompt success";
    }
    catch (NativeFailure error)
    {
      // Unknown identities may be closed before the hello reply. A transport
      // failure alone is not proof of authorization: the external harness must
      // correlate the PID with consentd's actual kernel-role rejection log.
      result = $"OBSERVED stage={stage} status={error.Status} requires-daemon-rejection-evidence";
    }
    catch (Exception error) { result = $"FAIL {error.GetType().Name}"; }
    string feature;
    try
    {
      new FeatureApi().Catalog();
      feature = "FAIL feature_status=0 unexpected catalog success";
    }
    catch (NativeFailure error)
    { feature = $"OBSERVED feature_status={error.Status} requires-argo-rejection-evidence"; }
    catch (Exception error) { feature = $"FAIL feature_exception={error.GetType().Name}"; }
    result = $"pid={Environment.ProcessId} {result} {feature}";
    try { File.WriteAllText(output, result + "\n"); }
    catch (Exception error) { Console.Error.WriteLine($"ConsentUI negative output {error.GetType().Name}"); }
    Console.WriteLine($"ConsentUI negative pid={Environment.ProcessId} {result}");
    Volatile.Write(ref complete, 1);
  }

  protected override void OnTerminate()
  {
    timer?.Stop();
    timer?.Dispose();
    base.OnTerminate();
  }

  private static void Main(string[] args)
  {
    var app = new NegativeApplication();
    try { app.Run(args); }
    finally { app.thread?.Join(6000); }
  }
}
