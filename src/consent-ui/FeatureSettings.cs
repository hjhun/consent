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
using System.Text.Json;

namespace ConsentUI;

internal sealed record FeatureMapping(bool TaskOnly, IReadOnlyDictionary<string, string> Values);

internal sealed record Feature(string Id, string Revision, string TitleEnglish, string TitleKorean,
    string DescriptionEnglish, string DescriptionKorean, string Provider,
    string ProviderPackage, string ProviderApp, IReadOnlyList<FeatureMapping> Mappings);

internal sealed class FeatureCatalog
{
  public IReadOnlyList<Feature> Features { get; }
  public IReadOnlyList<string> Tasks { get; }
  public IReadOnlyList<string> Presets { get; }
  public string SelectionRevision { get; }
  public string Revision { get; }
  public string CoordinatorEpoch { get; }
  public FeatureCatalog(string json)
  {
    using var document = FeatureJson.Parse(json);
    var root = document.RootElement;
    CoordinatorEpoch = FeatureJson.Identifier(root, "coordinator_epoch");
    SelectionRevision = FeatureJson.Positive(root, "selection_revision");
    Revision = FeatureJson.Text(root, "catalog_revision", 64);
    if (Revision.Length != 64 || !Revision.All(c => c is >= '0' and <= '9' or >= 'a' and <= 'f'))
      throw new InvalidOperationException("Invalid catalog revision");
    var features = new List<Feature>();
    var unique = new HashSet<string>(StringComparer.Ordinal);
    foreach (var item in root.GetProperty("features").EnumerateArray())
    {
      string id = FeatureJson.Identifier(item, "id");
      if (!unique.Add(id) || features.Count == 16) throw new InvalidOperationException("Invalid feature catalog");
      var mappings = new List<FeatureMapping>();
      foreach (var mapping in item.GetProperty("mappings").EnumerateArray())
      {
        if (mappings.Count == 8) throw new InvalidOperationException("Too many feature mappings");
        var values = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (string field in new[] { "definition", "policy_version", "operation", "scope", "purpose", "recipient", "holder", "retention_ms" })
          values[field] = FeatureJson.Text(mapping, field, 4096, field is "scope" or "recipient" or "holder");
        mappings.Add(new FeatureMapping(mapping.GetProperty("task_only").GetBoolean(),
            new System.Collections.ObjectModel.ReadOnlyDictionary<string, string>(values)));
      }
      if (mappings.Count == 0) throw new InvalidOperationException("Missing feature mapping");
      features.Add(new Feature(id, FeatureJson.Positive(item, "revision"),
          FeatureJson.Text(item, "title_en", 256), FeatureJson.Text(item, "title_ko", 256),
          FeatureJson.Text(item, "description_en", 2048), FeatureJson.Text(item, "description_ko", 2048),
          FeatureJson.Text(item, "provider_label", 256), FeatureJson.Identifier(item, "provider_package"),
          FeatureJson.Identifier(item, "provider_app"), mappings.AsReadOnly()));
    }
    if (features.Count == 0) throw new InvalidOperationException("Empty feature catalog");
    Features = features.AsReadOnly();
    var tasks = root.GetProperty("tasks").EnumerateArray().Select(value => value.GetString() ?? "").ToArray();
    if (tasks.Length is < 1 or > 6 || tasks.Distinct(StringComparer.Ordinal).Count() != tasks.Length ||
        tasks.Any(value => value is not ("calendar-summary" or "device-off" or "calendar-and-device" or "calendar-alternative" or "calendar-expanded" or "conversation-close")))
      throw new InvalidOperationException("Unknown fixed task");
    Tasks = Array.AsReadOnly(tasks);
    var modes = root.GetProperty("presets").EnumerateArray().Select(value => value.GetString() ?? "").ToArray();
    if (modes.Length is < 1 or > 3 || modes.Distinct(StringComparer.Ordinal).Count() != modes.Length ||
        modes.Any(value => value is not ("SESSION" or "TIMED")))
      throw new InvalidOperationException("Unknown period preset");
    Presets = Array.AsReadOnly(new[] { "SESSION", "TIMED" }.Where(modes.Contains).ToArray());
  }
  public Feature Match(IReadOnlyDictionary<string, string> fields, string prefix)
  {
    foreach (var feature in Features)
    {
      if (feature.Id != fields.GetValueOrDefault(prefix + "feature_id") ||
          feature.Revision != fields.GetValueOrDefault(prefix + "feature_revision") ||
          feature.ProviderPackage != fields.GetValueOrDefault(prefix + "provider_package") ||
          feature.ProviderApp != fields.GetValueOrDefault(prefix + "provider_app")) continue;
      foreach (var mapping in feature.Mappings)
        if (mapping.Values.All(field => fields.TryGetValue(prefix + field.Key, out string? value) && value == field.Value))
          return feature;
    }
    throw new InvalidOperationException("Unrecognized feature mapping");
  }

}

internal sealed class FeatureStatus
{
  public string Decision { get; }
  public string RequestId { get; }
  public string SelectionId { get; }
  public string Revision { get; }
  public string Digest { get; }
  public string Mode { get; }
  public string Duration { get; }
  public string Session { get; }
  public string Generation { get; }
  public IReadOnlyList<string> Selected { get; }
  public string CoordinatorEpoch { get; }
  public string CatalogRevision { get; }
  public string SelectedMode { get; }
  public string SelectedDuration { get; }
  public bool ExecutionPending { get; }
  public string CleanupState { get; }
  public string CleanupPending { get; }
  public bool CleanupComplete => Decision == "ALLOWED" && CleanupState == "CLOSED" && CleanupPending == "0";
  public FeatureStatus(string json)
  {
    using var document = FeatureJson.Parse(json);
    var root = document.RootElement;
    CoordinatorEpoch = FeatureJson.Identifier(root, "coordinator_epoch");
    CatalogRevision = FeatureJson.Text(root, "catalog_revision", 64);
    Decision = FeatureJson.Text(root, "decision", 32);
    if (Decision is not ("IDLE" or "PENDING" or "ALLOWED" or "DENIED" or "INVALIDATED" or "CONSENT_REQUIRED"))
      throw new InvalidOperationException("Unknown feature decision");
    RequestId = FeatureJson.Text(root, "request_id", 128, true);
    if (RequestId.Length != 0 && !PromptSnapshot.ValidRequestId(RequestId))
      throw new InvalidOperationException("Invalid feature request");
    SelectionId = FeatureJson.Text(root, "selection_id", 128, true);
    Revision = FeatureJson.Positive(root, "selection_revision");
    Digest = FeatureJson.Text(root, "selection_digest", 64, true);
    Mode = FeatureJson.Text(root, "grant_mode", 16, true);
    Duration = FeatureJson.Text(root, "duration_ms", 16);
    SelectedMode = FeatureJson.Text(root, "settings_grant_mode", 16, true);
    SelectedDuration = FeatureJson.Text(root, "settings_duration_ms", 16);
    string executing = FeatureJson.Text(root, "execution_pending", 1);
    if (executing is not ("0" or "1")) throw new InvalidOperationException("Invalid execution phase");
    ExecutionPending = executing == "1";
    CleanupState = FeatureJson.Text(root, "cleanup_state", 32, true);
    CleanupPending = FeatureJson.Text(root, "cleanup_pending", 32, true);
    Session = FeatureJson.Text(root, "session", 128, true);
    Generation = FeatureJson.Text(root, "generation", 32, true);
    var selected = root.GetProperty("selected").EnumerateArray().Select(item => item.GetString() ?? "").ToArray();
    if (selected.Length > 16 || selected.Distinct(StringComparer.Ordinal).Count() != selected.Length ||
        selected.Any(id => !PromptSnapshot.ValidRequestId(id)))
      throw new InvalidOperationException("Invalid feature selection");
    Selected = Array.AsReadOnly(selected);
  }

  public bool Matches(PromptSnapshot prompt) => prompt.VersionedApproval &&
      prompt.RequestId == RequestId && prompt.Fields.GetValueOrDefault("selection_id") == SelectionId &&
      prompt.Fields.GetValueOrDefault("selection_revision") == Revision &&
      prompt.Fields.GetValueOrDefault("selection_digest") == Digest && prompt.GrantMode == Mode &&
      (Mode != "TIMED" || prompt.Fields.GetValueOrDefault("duration_ms") == Duration) &&
      prompt.Fields.GetValueOrDefault("session", "") == Session &&
      prompt.Fields.GetValueOrDefault("generation", "") == Generation;
}

internal static class FeatureJson
{
  public static JsonDocument Parse(string json)
  {
    if (Encoding.UTF8.GetByteCount(json) > 65536) throw new InvalidOperationException("Oversized feature response");
    var document = JsonDocument.Parse(json, new JsonDocumentOptions { MaxDepth = 8 });
    try
    {
      ValidateUnique(document.RootElement);
      if (document.RootElement.GetProperty("schema").GetInt32() != 1)
        throw new InvalidOperationException("Unknown feature schema");
      return document;
    }
    catch { document.Dispose(); throw; }
  }
  private static void ValidateUnique(JsonElement item)
  {
    if (item.ValueKind == JsonValueKind.Object)
    {
      var keys = new HashSet<string>(StringComparer.Ordinal);
      foreach (var field in item.EnumerateObject())
      { if (!keys.Add(field.Name)) throw new InvalidOperationException("Duplicate feature field"); ValidateUnique(field.Value); }
    }
    else if (item.ValueKind == JsonValueKind.Array)
      foreach (var value in item.EnumerateArray()) ValidateUnique(value);
  }
  public static string Text(JsonElement root, string key, int maximum, bool empty = false)
  {
    string value = root.GetProperty(key).GetString() ?? throw new InvalidOperationException("Invalid feature field type");
    if ((!empty && value.Length == 0) || Encoding.UTF8.GetByteCount(value) > maximum)
      throw new InvalidOperationException("Invalid feature field length");
    return value;
  }
  public static string Identifier(JsonElement root, string key)
  {
    string value = Text(root, key, 128);
    if (!PromptSnapshot.ValidRequestId(value)) throw new InvalidOperationException("Invalid feature identifier");
    return value;
  }
  public static string Positive(JsonElement root, string key)
  {
    string value = Text(root, key, 32);
    if (!long.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out long number) ||
        number < 1 || number.ToString(CultureInfo.InvariantCulture) != value)
      throw new InvalidOperationException("Invalid feature revision");
    return value;
  }
}

internal sealed class FeatureSelection
{
  public FeatureCatalog Catalog { get; }
  private readonly HashSet<string> selected = new(StringComparer.Ordinal);
  public string? Mode { get; private set; }
  public string Revision { get; set; }
  public string TaskId { get; private set; }
  public bool Ready => Mode is not null;
  public bool HasSelection => selected.Count != 0;
  public bool Dirty { get; private set; }
  public string Ids => string.Join(',', selected.Order(StringComparer.Ordinal));
  public uint Duration => Mode == "TIMED" ? 1800000u : 0u;
  public FeatureSelection(FeatureCatalog catalog)
  { Catalog = catalog; Revision = catalog.SelectionRevision; TaskId = catalog.Tasks[0]; }
  public bool IsSelected(string id) => selected.Contains(id);
  public void Toggle(string id)
  {
    if (!Catalog.Features.Any(feature => feature.Id == id)) throw new InvalidOperationException("Unknown feature");
    if (!selected.Remove(id)) selected.Add(id);
    Dirty = true;
  }
  public void NextMode()
  {
    int index = Array.IndexOf(Catalog.Presets.ToArray(), Mode);
    Mode = Catalog.Presets[(index + 1) % Catalog.Presets.Count];
    Dirty = true;
  }
  public void Restore(FeatureStatus status)
  {
    if (status.CoordinatorEpoch != Catalog.CoordinatorEpoch || status.CatalogRevision != Catalog.Revision ||
        status.Selected.Any(id => !Catalog.Features.Any(feature => feature.Id == id)) ||
        status.SelectedMode is not ("" or "SESSION" or "TIMED") ||
        (status.SelectedMode == "TIMED" ? status.SelectedDuration != "1800000" : status.SelectedDuration != "0"))
      throw new InvalidOperationException("Invalid committed selection");
    selected.Clear(); foreach (string id in status.Selected) selected.Add(id);
    Mode = status.SelectedMode.Length == 0 ? null : status.SelectedMode;
    Revision = status.Revision;
    Dirty = false;
  }
  public void Reconcile(FeatureStatus status, bool requireReview = false)
  {
    if (status.CoordinatorEpoch != Catalog.CoordinatorEpoch || status.CatalogRevision != Catalog.Revision)
      throw new InvalidOperationException("Feature coordinator changed");
    // A readonly execution-status poll must not undo the user's local edits.
    if (!Dirty || Revision != status.Revision || requireReview) Restore(status);
    if (requireReview) Dirty = true;
  }
  public void NextTask()
  {
    int index = Array.IndexOf(Catalog.Tasks.ToArray(), TaskId);
    TaskId = Catalog.Tasks[(index + 1) % Catalog.Tasks.Count];
  }
}
