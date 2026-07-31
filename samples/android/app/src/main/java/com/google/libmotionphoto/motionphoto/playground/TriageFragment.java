// Copyright 2026 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package com.google.libmotionphoto.motionphoto.playground;

import android.content.Context;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import androidx.annotation.NonNull;
import androidx.fragment.app.Fragment;

/** Fragment for the initial triage screen. */
public class TriageFragment extends Fragment {
  private NavigationListener listener;

  @Override
  public void onAttach(@NonNull Context context) {
    super.onAttach(context);
    if (context instanceof NavigationListener) {
      listener = (NavigationListener) context;
    }
  }

  @Override
  public void onDetach() {
    super.onDetach();
    listener = null;
  }

  @Override
  public View onCreateView(
      LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {
    View view = inflater.inflate(R.layout.fragment_triage, container, false);

    view.findViewById(R.id.btn_creator_demo)
        .setOnClickListener(
            v -> {
              if (listener != null) {
                listener.navigateToCreatorImage();
              }
            });

    view.findViewById(R.id.btn_capture_demo)
        .setOnClickListener(
            v -> {
              if (listener != null) {
                listener.navigateToCaptureDemo();
              }
            });

    view.findViewById(R.id.btn_extractor_demo)
        .setOnClickListener(
            v -> {
              if (listener != null) {
                listener.navigateToExtractorSelect();
              }
            });

    return view;
  }
}
