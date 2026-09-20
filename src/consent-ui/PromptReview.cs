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
namespace ConsentUI;

internal sealed class PromptReview
{
  public PromptSnapshot? Snapshot { get; private set; }
  public int Page { get; private set; }
  public int Reviewed { get; private set; }
  public bool CanAllow => Snapshot is not null && Snapshot.CanAllowOnce && Reviewed == Snapshot.Pages.Count;

  public int PageFor(PromptSnapshot fresh) => Snapshot is not null && Snapshot.SameContent(fresh) ? Page : 0;

  // Called only after the NUI renderer prepared and checked the candidate page.
  public void Install(PromptSnapshot fresh)
  {
    if (Snapshot is null || !Snapshot.SameContent(fresh)) { Page = 0; Reviewed = 0; }
    Snapshot = fresh;
    Reviewed = Math.Max(Reviewed, Page + 1);
  }

  public void Move(int offset)
  {
    if (offset is not (-1 or 1) || Snapshot is null || Page + offset < 0 || Page + offset >= Snapshot.Pages.Count)
      throw new InvalidOperationException("Invalid page navigation");
    Page += offset;
    Reviewed = Math.Max(Reviewed, Page + 1);
  }
}
