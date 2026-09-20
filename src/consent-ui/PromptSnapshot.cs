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
  public bool CanAllowOnce { get; }
  private readonly string content;

  public PromptSnapshot(string requestId, string locale,
      IReadOnlyDictionary<string, string> fields, IReadOnlyList<(string Title, string Body)> text)
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
    content = stable.ToString();
    var pages = new List<string>();
    bool once = true;
    bool korean = locale.StartsWith("ko", StringComparison.Ordinal);
    for (int i = 0; i < count; ++i)
    {
      string prefix = $"r{i}.";
      foreach (string required in new[] { "definition", "policy_version", "text_revision", "locale", "modes" })
        if (string.IsNullOrEmpty(fields.GetValueOrDefault(prefix + required)))
          throw new InvalidOperationException("Missing bound prompt field");
      once &= fields[prefix + "modes"].Split(',').Contains("ONCE", StringComparer.Ordinal);
      string heading = korean ? $"조건 {i + 1}/{count}" : $"Condition {i + 1}/{count}";
      string details = $"{heading}\n{text[i].Title}\n\n{text[i].Body}";
      pages.AddRange(Paginate(details));
    }
    if (pages.Count > 1024) throw new InvalidOperationException("Prompt is too large to display");
    CanAllowOnce = once;
    Pages = pages.AsReadOnly();
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
