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

internal interface IConsentBackend : IDisposable
{
  PromptSnapshot Fetch(string requestId, string locale);
  string Respond(PromptSnapshot snapshot, bool allow);
}

internal sealed class NativeApi : IConsentBackend
{
  // This library is built with the isolated PoC endpoint. Never substitute the
  // production library or accept a library/socket name through launch data.
  private const string Library = "libconsent-poc.so.0";
  private IntPtr context;
  private IntPtr client;
  private FeatureCatalog? catalog;

  public NativeApi()
  {
    context = g_main_context_new();
    if (context == IntPtr.Zero) throw new OutOfMemoryException();
    try { Check(consent_client_create_with_context(context, out client)); }
    catch { g_main_context_unref(context); context = IntPtr.Zero; throw; }
  }

  public PromptSnapshot Fetch(string requestId, string locale)
  {
    using var parameters = new Parameters();
    parameters.Set("request_id", requestId);
    parameters.Set("locale", locale);
    parameters.Set("template_version", "1");
    parameters.Set("approval_version", "1");
    IntPtr result = IntPtr.Zero;
    try
    {
      Check(consent_get_prompt(client, parameters.Handle, out result));
      var fields = new Dictionary<string, string>(StringComparer.Ordinal);
      nuint size = consent_result_size(result);
      if (size > 256) throw new InvalidOperationException("Invalid native prompt size");
      for (nuint i = 0; i < size; ++i)
      {
        Check(consent_result_get_at(result, i, out var key, out var value));
        fields.Add(Utf8(key), Utf8(value));
      }
      string terminal = fields.GetValueOrDefault("decision", "");
      if (string.IsNullOrEmpty(fields.GetValueOrDefault("prompt_token")) &&
          terminal is "ALLOWED" or "DENIED" or "INVALIDATED" or "CANCELLED" or "EXPIRED")
        throw new PromptFinished(terminal);
      if (!int.TryParse(fields.GetValueOrDefault("count"), out int count) || count < 1 || count > 16)
        throw new InvalidOperationException("Invalid native requirement count");
      var text = new List<(string, string)>();
      for (uint i = 0; i < count; ++i)
        text.Add((Format(result, i, "title"), Format(result, i, "body")));
      if (fields.ContainsKey("approval_version"))
      {
        catalog ??= new FeatureApi().Catalog();
        for (int i = 0; i < count; ++i) catalog.Match(fields, $"r{i}.");
      }
      return new PromptSnapshot(requestId, locale, fields, text, catalog);
    }
    finally { consent_result_free(result); }
  }

  public string Respond(PromptSnapshot snapshot, bool allow)
  {
    using var parameters = new Parameters();
    foreach (var field in snapshot.ResponseFields(allow)) parameters.Set(field.Key, field.Value);
    IntPtr result = IntPtr.Zero;
    try
    {
      Check(consent_respond(client, parameters.Handle, out result));
      return Utf8(consent_result_get(result, "decision"));
    }
    finally { consent_result_free(result); }
  }

  public void Dispose()
  {
    if (client != IntPtr.Zero)
    {
      int status = consent_client_destroy(client);
      client = IntPtr.Zero;
      if (status != 0) Console.Error.WriteLine($"ConsentUI destroy status={status}");
    }
    if (context != IntPtr.Zero) { g_main_context_unref(context); context = IntPtr.Zero; }
  }

  private static string Format(IntPtr result, uint index, string field)
  {
    IntPtr text = IntPtr.Zero;
    try { Check(consent_prompt_format(result, index, field, out text)); return Utf8(text); }
    finally { free(text); }
  }
  private static string Utf8(IntPtr value) => Marshal.PtrToStringUTF8(value) ?? string.Empty;
  private static void Check(int status)
  {
    if (status != 0) throw new NativeFailure(status);
  }

  private sealed class Parameters : IDisposable
  {
    public IntPtr Handle { get; private set; }
    public Parameters() { Check(consent_params_create(out var value)); Handle = value; }
    public void Set(string key, string value) => Check(consent_params_set(Handle, key, value));
    public void Dispose() { consent_params_free(Handle); Handle = IntPtr.Zero; }
  }

  [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
  private static extern int consent_client_create_with_context(IntPtr context, out IntPtr client);
  [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
  private static extern int consent_client_destroy(IntPtr client);
  [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
  private static extern int consent_params_create(out IntPtr parameters);
  [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
  private static extern void consent_params_free(IntPtr parameters);
  [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
  private static extern int consent_params_set(IntPtr parameters,
      [MarshalAs(UnmanagedType.LPUTF8Str)] string key, [MarshalAs(UnmanagedType.LPUTF8Str)] string value);
  [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
  private static extern int consent_get_prompt(IntPtr client, IntPtr parameters, out IntPtr result);
  [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
  private static extern int consent_respond(IntPtr client, IntPtr parameters, out IntPtr result);
  [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
  private static extern void consent_result_free(IntPtr result);
  [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
  private static extern nuint consent_result_size(IntPtr result);
  [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
  private static extern int consent_result_get_at(IntPtr result, nuint index, out IntPtr key, out IntPtr value);
  [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
  private static extern IntPtr consent_result_get(IntPtr result, [MarshalAs(UnmanagedType.LPUTF8Str)] string key);
  [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
  private static extern int consent_prompt_format(IntPtr result, uint index,
      [MarshalAs(UnmanagedType.LPUTF8Str)] string field, out IntPtr text);
  [DllImport("libglib-2.0.so.0", CallingConvention = CallingConvention.Cdecl)]
  private static extern IntPtr g_main_context_new();
  [DllImport("libglib-2.0.so.0", CallingConvention = CallingConvention.Cdecl)]
  private static extern void g_main_context_unref(IntPtr context);
  [DllImport("libc.so.6", CallingConvention = CallingConvention.Cdecl)]
  private static extern void free(IntPtr value);
}

internal sealed class NativeFailure : Exception
{
  public int Status { get; }
  public NativeFailure(int status) : base($"Native consent status {status}") { Status = status; }
}

internal sealed class PromptFinished : Exception
{
  public string Decision { get; }
  public PromptFinished(string decision) { Decision = decision; }
}
