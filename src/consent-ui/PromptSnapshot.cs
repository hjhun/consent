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
using System.Globalization;
using System.Text;

namespace ConsentUI;

internal sealed class PromptSnapshot
{
  public string RequestId { get; }
  public string Token { get; }
  public string Locale { get; }
  public IReadOnlyDictionary<string, string> Fields { get; }
  public IReadOnlyList<string> Pages { get; }
  public bool CanApprove { get; }
  public bool CanAllowOnce => CanApprove && GrantMode == "ONCE";
  public bool VersionedApproval { get; }
  public string GrantMode { get; }
  public long DurationMilliseconds { get; }
  public int TotalCount { get; }
  private readonly string content;

  public PromptSnapshot(string requestId, string locale,
      IReadOnlyDictionary<string, string> fields, IReadOnlyList<(string Title, string Body)> text,
      FeatureCatalog? catalog = null)
  {
    if (!ValidRequestId(requestId) || !ValidLocale(locale) ||
        !fields.TryGetValue("request_id", out var actual) || actual != requestId ||
        !fields.TryGetValue("prompt_token", out var token) || !ValidRequestId(token) ||
        !fields.TryGetValue("count", out var countText) ||
        !int.TryParse(countText, NumberStyles.None, CultureInfo.InvariantCulture, out var count) ||
        count < 1 || count > 16 || count != text.Count)
      throw new InvalidOperationException("Invalid prompt envelope");
    if (fields.GetValueOrDefault("template_version") == "1" &&
        fields.GetValueOrDefault("locale") != locale)
      throw new InvalidOperationException("Prompt locale mismatch");
    VersionedApproval = fields.ContainsKey("approval_version");
    GrantMode = VersionedApproval ? Required(fields, "grant_mode") : "ONCE";
    TotalCount = count;
    if (VersionedApproval)
    {
      if (Required(fields, "approval_version") != "1" ||
          Required(fields, "request_kind") is not ("PREAPPROVAL" or "TASK") ||
          !ValidRequestId(Required(fields, "selection_id")) ||
          Positive(fields, "selection_revision") < 1 ||
          Required(fields, "selection_digest") is not { Length: 64 } digest ||
          !digest.All(c => c is >= '0' and <= '9' or >= 'a' and <= 'f') ||
          GrantMode is not ("ONCE" or "SESSION" or "TIMED"))
        throw new InvalidOperationException("Invalid approval binding");
      TotalCount = checked((int)Positive(fields, "total_count"));
      if (TotalCount < count || TotalCount > 16)
        throw new InvalidOperationException("Invalid approval condition count");
      if (GrantMode == "SESSION" &&
          (!ValidRequestId(Required(fields, "session")) || Positive(fields, "generation") < 1))
        throw new InvalidOperationException("Missing session binding");
      if (GrantMode == "TIMED")
      {
        DurationMilliseconds = Positive(fields, "duration_ms");
        if (DurationMilliseconds < 100 || DurationMilliseconds > 3600000)
          throw new InvalidOperationException("Invalid approval duration");
      }
      else if (fields.ContainsKey("duration_ms"))
        throw new InvalidOperationException("Unexpected approval duration");
    }
    RequestId = requestId;
    Locale = locale;
    Token = token;
    Fields = new System.Collections.ObjectModel.ReadOnlyDictionary<string, string>(
        new Dictionary<string, string>(fields, StringComparer.Ordinal));
    var stable = new StringBuilder(locale);
    foreach (var field in fields.OrderBy(item => item.Key, StringComparer.Ordinal))
    {
      // Transport correlation and refreshed tokens do not change displayed text.
      if (field.Key is "prompt_token" or "id" or "status" or "revision" or "source") continue;
      stable.Append(field.Key.Length).Append(':').Append(field.Key)
          .Append(field.Value.Length).Append(':').Append(field.Value);
    }
    var pages = new List<string>();
    bool supported = true;
    var originalIndices = new HashSet<int>();
    bool korean = locale.StartsWith("ko", StringComparison.Ordinal);
    for (int i = 0; i < count; ++i)
    {
      string prefix = $"r{i}.";
      foreach (string required in new[] { "definition", "policy_version", "text_revision", "locale", "modes" })
        if (string.IsNullOrEmpty(fields.GetValueOrDefault(prefix + required)))
          throw new InvalidOperationException("Missing bound prompt field");
      supported &= fields[prefix + "modes"].Split(',').Contains(GrantMode, StringComparer.Ordinal);
      if (VersionedApproval)
      {
        int original = checked((int)CanonicalNumber(fields, prefix + "original_index", 0));
        if (original >= TotalCount || !originalIndices.Add(original) ||
            !ValidRequestId(Required(fields, prefix + "feature_id")) ||
            Positive(fields, prefix + "feature_revision") < 1 ||
            !ValidRequestId(Required(fields, prefix + "provider_package")) ||
            !ValidRequestId(Required(fields, prefix + "provider_app")))
          throw new InvalidOperationException("Invalid approval feature binding");
      }
      string heading = korean ? $"조건 {i + 1}/{count}" : $"Condition {i + 1}/{count}";
      string details = $"{heading}\n{text[i].Title}\n\n{text[i].Body}";
      if (VersionedApproval)
      {
        var feature = (catalog ?? throw new InvalidOperationException("Missing protected catalog")).Match(fields, prefix);
        var mapping = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (string key in new[] { "definition", "policy_version", "operation", "scope", "purpose", "recipient", "holder", "retention_ms" })
          mapping[key] = fields[prefix + key];
        string metadata = FeatureDisplay.Metadata(feature, mapping, PeriodLabel(korean), korean);
        details = $"{heading}\n{metadata}\n\n{text[i].Title}\n{text[i].Body}";
      }
      pages.AddRange(Paginate(details));
    }
    if (pages.Count > 1024) throw new InvalidOperationException("Prompt is too large to display");
    CanApprove = supported;
    foreach (string page in pages) stable.Append(page.Length).Append(':').Append(page);
    content = stable.ToString();
    Pages = pages.AsReadOnly();
  }

  public string PeriodLabel(bool korean) => GrantMode switch
  {
    "ONCE" => korean ? "이번 한 번" : "One access only",
    "SESSION" => korean ? "이번 대화 동안" : "For this conversation",
    "TIMED" when DurationMilliseconds % 60000 == 0 => korean ?
        $"승인 후 {DurationMilliseconds / 60000}분" : $"For {DurationMilliseconds / 60000} minutes after approval",
    "TIMED" => korean ? $"승인 후 {DurationMilliseconds}밀리초" : $"For {DurationMilliseconds} milliseconds after approval",
    _ => throw new InvalidOperationException("Unsupported approval duration"),
  };

  public IReadOnlyDictionary<string, string> ResponseFields(bool allow)
  {
    if (allow && !CanApprove) throw new InvalidOperationException("Unsupported approval mode");
    var response = new Dictionary<string, string>(StringComparer.Ordinal)
    {
      ["request_id"] = RequestId, ["prompt_token"] = Token, ["locale"] = Locale,
      ["decision"] = allow ? "ALLOWED" : "DENIED", ["grant_mode"] = GrantMode,
    };
    foreach (var field in Fields)
    {
      bool context = field.Key is "subject" or "profile" or "session" or "generation";
      bool approval = VersionedApproval && field.Key is "approval_version" or "request_kind" or
          "selection_id" or "selection_revision" or "selection_digest" or "duration_ms" or "count" or "total_count";
      bool row = field.Key.StartsWith("r", StringComparison.Ordinal) &&
          (field.Key.EndsWith(".policy_version", StringComparison.Ordinal) ||
           field.Key.EndsWith(".text_revision", StringComparison.Ordinal) ||
           VersionedApproval && (field.Key.EndsWith(".original_index", StringComparison.Ordinal) ||
             field.Key.EndsWith(".feature_id", StringComparison.Ordinal) ||
             field.Key.EndsWith(".feature_revision", StringComparison.Ordinal) ||
             field.Key.EndsWith(".provider_package", StringComparison.Ordinal) ||
             field.Key.EndsWith(".provider_app", StringComparison.Ordinal)));
      if (context || approval || row) response[field.Key] = field.Value;
    }
    return new System.Collections.ObjectModel.ReadOnlyDictionary<string, string>(response);
  }

  private static string Required(IReadOnlyDictionary<string, string> fields, string key) =>
      fields.TryGetValue(key, out var value) && value.Length != 0 ? value :
      throw new InvalidOperationException("Missing approval binding");
  private static long Positive(IReadOnlyDictionary<string, string> fields, string key) => CanonicalNumber(fields, key, 1);
  private static long CanonicalNumber(IReadOnlyDictionary<string, string> fields, string key, long minimum)
  {
    string value = Required(fields, key);
    if (!long.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out long number) ||
        number < minimum || number.ToString(CultureInfo.InvariantCulture) != value)
      throw new InvalidOperationException("Invalid numeric approval binding");
    return number;
  }

  public bool SameContent(PromptSnapshot other) => content == other.content;

  public static bool ValidRequestId(string? value) => value is { Length: > 0 and <= 128 } &&
      value.All(c => char.IsAsciiLetterOrDigit(c) || c is '-' or '_' or '.');

  public static bool ValidLocale(string? value) => value is "en" or "en-US" or "en-GB" or "ko" or "ko-KR";

  internal static IReadOnlyList<string> Paginate(string text)
  {
    // Explicit wrapping bounds each page; no ellipsis or hidden unreviewed rows.
    // Reject invisible formatting controls instead of interpreting them as UI.
    text = text.Replace("\r\n", "\n", StringComparison.Ordinal).Replace('\t', ' ');
    if (text.Any(c => (char.IsControl(c) && c != '\n') ||
        char.GetUnicodeCategory(c) == UnicodeCategory.Format))
      throw new InvalidOperationException("Unsupported display controls");
    var pages = new List<string>();
    var page = new StringBuilder();
    int lines = 1;
    int columns = 0;
    var elements = StringInfo.GetTextElementEnumerator(text);
    while (elements.MoveNext())
    {
      string element = elements.GetTextElement();
      if (element.Length > 16) throw new InvalidOperationException("Display cluster is too large");
      if (element == "\n" || columns == 26)
      {
        if (lines == 6)
        {
          pages.Add(page.ToString());
          page.Clear();
          lines = 1;
        }
        else { page.Append('\n'); ++lines; }
        columns = 0;
        if (element == "\n") continue;
      }
      page.Append(element);
      ++columns;
    }
    if (page.Length != 0) pages.Add(page.ToString());
    return pages;
  }
}
