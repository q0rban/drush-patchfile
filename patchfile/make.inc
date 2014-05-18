<?php

class drush_patchfile_make extends drush_patchfile {

  public function getProjectPatches($project_name = NULL) {
    $projects = array();

    if (!empty($this->file)) {
      // Cache not only by filename, but also by the time the file was modified
      // so that a drush cache clear is not necessary to pick up changes.
      $cid = $this->getFileCid($this->file);
      if ($cid && $this->useCache() && $cache = drush_cache_get($cid)) {
        $projects = $cache->data;
      }
      else {
        $projects = $this->parseInfoFile($this->file);
        if ($cid && $this->useCache()) {
          drush_cache_set($cid, $projects);
        }
      }
    }
    else {
      // @todo Error here.
    }

    if (isset($project_name)) {
      return isset($projects[$project_name]) ? $projects[$project_name] : array();
    }
    else {
      return $projects;
    }
  }

  public function addPatch($project_name, $patch_url) {
    $output = "\nprojects[{$project_name}][patch][] = \"{$patch_url}\"\n";
    $result = file_put_contents($this->file, $output, FILE_APPEND);
    if ($result !== FALSE) {
      drush_log(dt('Added patch @patch to @project in @file.', array('@patch' => $patch_url, '@project' => $project_name, '@file' => $this->file)), 'success');
      return TRUE;
    }
    else {
      return drush_set_error('DRUSH_MAKE_FILE_NO_WRITE', dt('Unable to append to file @file.', array('@this->file' => $file)));
    }
  }

  private function getFileCid($file) {
    $params = array();
    $includes = $this->getAllFiles($file);
    foreach ($includes as $include) {
      $params[] = $include . ':' . filemtime($include);
    }
    return drush_get_cid('patchfile', array(), $params);
  }

  /**
   * A copy of make_file_parse_info() but that only returns an array of files.
   *
   * @see make_file_parse_info()
   */
  private function getAllFiles($makefile) {
    $data = make_get_data($makefile);
    if (empty($data)) {
      drush_set_error('MAKE_INVALID_MAKE_FILE', dt('Invalid or empty make file: !makefile', array('!makefile' => $makefile)));
      return array();
    }

    $info = _drush_drupal_parse_info_file($data);
    if (empty($info)) {
      return array();
    }

    $includes = array();

    if (!empty($info['includes']) && is_array($info['includes'])) {
      $include_path = dirname($makefile);
      foreach ($info['includes'] as $include) {
        if (is_string($include)) {
          if (make_valid_url($include, TRUE) && ($file = $this->getAllFiles($include))) {
            $includes = array_merge($includes, $file);
          }
          elseif (file_exists($include_path . '/' . $include) && ($file = $this->getAllFiles($include_path . '/' . $include))) {
            $includes = array_merge($includes, $file);
          }
          elseif (file_exists($include) && ($file = $this->getAllFiles($include))) {
            $includes = array_merge($includes, $file);
          }
          else {
            make_error('BUILD_ERROR', dt("Include file missing: !include", array('!include' => $include)));
          }
        }
      }
    }

    return array_merge(array($makefile), $includes);
  }

  private function parseInfoFile($file) {
    // Parse the patch make file.
    $info = make_parse_info_file($file);

    if ($info === FALSE) {
      // Throw fatal error here.
    }

    if (!empty($info['projects'])) {
      foreach ($info['projects'] as $project_name => &$project_info) {
        // Only return projects that have patches listed.
        if (empty($project_info['patch'])) {
          unset($info['projects'][$project_name]);
          continue;
        }

        // Normalize each patch record.
        foreach ($project_info['patch'] as &$patch_info) {
          _drush_patchfile_preprocess_local_file_url($patch_info, $file);
        }

        // Move the patches array down one level to the project itself.
        $info['projects'][$project_name] = $project_info['patch'];
      }
    }

    return $info['projects'];
  }
}
