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
using System.Runtime.InteropServices;

namespace ConsentUI;

internal interface IFeatureBackend
{
  FeatureCatalog Catalog();
  FeatureStatus Status();
  FeatureStatus Select(string ids, string mode, uint duration, string epoch, string catalog, string revision, string command);
  FeatureStatus Preapprove(string epoch, string catalog, string revision, string command);
  FeatureStatus Task(string taskId, string taskMode, string epoch, string catalog, string revision, string command);
}

internal sealed class FeatureApi : IFeatureBackend
{
  private const string Library = "libconsent-feature-poc.so.0";
  private delegate int Call(out IntPtr result);
  private static string Invoke(Call call)
  {
    IntPtr value = IntPtr.Zero;
    try
    {
      int status = call(out value);
      if (status != 0) throw new NativeFailure(status);
      return Marshal.PtrToStringUTF8(value) ?? throw new InvalidOperationException("Missing feature response");
    }
    finally { consent_feature_free(value); }
  }
  public FeatureCatalog Catalog() => new(Invoke(consent_feature_catalog));
  public FeatureStatus Status() => new(Invoke(consent_feature_status));
  public FeatureStatus Select(string ids, string mode, uint duration, string epoch, string catalog, string revision, string command) =>
      new(Invoke((out IntPtr result) => consent_feature_select(ids, mode, duration, epoch, catalog, revision, command, out result)));
  public FeatureStatus Preapprove(string epoch, string catalog, string revision, string command) =>
      new(Invoke((out IntPtr result) => consent_feature_preapprove(epoch, catalog, revision, command, out result)));
  public FeatureStatus Task(string taskId, string taskMode, string epoch, string catalog, string revision, string command) =>
      new(Invoke((out IntPtr result) => consent_feature_task(taskId, taskMode, epoch, catalog, revision, command, out result)));

  [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
  private static extern int consent_feature_catalog(out IntPtr result);
  [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
  private static extern int consent_feature_status(out IntPtr result);
  [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
  private static extern int consent_feature_select([MarshalAs(UnmanagedType.LPUTF8Str)] string ids,
      [MarshalAs(UnmanagedType.LPUTF8Str)] string mode, uint duration,
      [MarshalAs(UnmanagedType.LPUTF8Str)] string epoch,
      [MarshalAs(UnmanagedType.LPUTF8Str)] string catalog,
      [MarshalAs(UnmanagedType.LPUTF8Str)] string revision,
      [MarshalAs(UnmanagedType.LPUTF8Str)] string command, out IntPtr result);
  [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
  private static extern int consent_feature_preapprove([MarshalAs(UnmanagedType.LPUTF8Str)] string epoch,
      [MarshalAs(UnmanagedType.LPUTF8Str)] string catalog,
      [MarshalAs(UnmanagedType.LPUTF8Str)] string revision,
      [MarshalAs(UnmanagedType.LPUTF8Str)] string command, out IntPtr result);
  [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
  private static extern int consent_feature_task([MarshalAs(UnmanagedType.LPUTF8Str)] string taskId,
      [MarshalAs(UnmanagedType.LPUTF8Str)] string taskMode,
      [MarshalAs(UnmanagedType.LPUTF8Str)] string epoch,
      [MarshalAs(UnmanagedType.LPUTF8Str)] string catalog,
      [MarshalAs(UnmanagedType.LPUTF8Str)] string revision,
      [MarshalAs(UnmanagedType.LPUTF8Str)] string command, out IntPtr result);
  [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
  private static extern void consent_feature_free(IntPtr result);
}
