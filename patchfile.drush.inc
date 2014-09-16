<?php

require_once __DIR__ . '/src/DrushPatchFileGit.php';

/**
 * @file
 * Drush integration for patch make files.
 */

/**
 * Implementation of hook_drush_engine_type_info().
 */
function patchfile_drush_engine_type_info() {
  $info = array();
  $info['patchfile'] = array(
    'description' => 'Patch file engines for reading and adding patch records to and from a file.',
    'option' => 'patch-file-engine',
  );
  return $info;
}

/**
 * Implements hook_drush_engine_ENGINE_TYPE().
 */
function patchfile_drush_engine_patchfile() {
  $engines = array();
  $engines['make'] = array(
    'description' => 'Uses drush make files for patch storage.',
  );
  return $engines;
}

/**
 * Implements hook_drush_command().
 */
function patchfile_drush_command() {
  $options = array(
    'patch-file' => 'Location of the patch make file.',
    'patch-file-engine' => 'The engine to use when interacting with the patch file.',
  );

  $items['patch-status'] = array(
    'description' => 'List the patches that should be applied to the site.',
    'options' => $options,
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_FULL,
    'aliases' => array('ps'),
  );
  $items['patch-project'] = array(
    'description' => 'List the patches that should be applied to the site.',
    'arguments' => array(
      'project-name' => 'The name of the module, theme, profile, or \'core\' to re-apply patches to.',
      'project-directory' => 'Optional, the directory of the project to use for the patches.',
    ),
    'required-arguments' => 1,
    'options' => array(
      'select' => 'If provided, will prompt for which patch to apply.',
    ) + $options,
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_FULL,
    'aliases' => array('pp'),
  );
  $items['patch-add'] = array(
    'description' => 'Add a new patch to the make file.',
    'arguments' => array(
      'project-name' => 'The name of the module, theme, profile, or \'core\' to re-apply patches to.',
      'patch-file' => 'The path to the patch to apply. Can be an external URL.',
    ),
    'required-arguments' => TRUE,
    'options' => array(
      'no-apply' => 'Do not apply the patch to the project.',
    ) + $options,
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_FULL,
    'aliases' => array('pa'),
  );
  $items['patch-apply-all'] = array(
    'description' => 'Apply all patches',
    'options' => $options,
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_FULL,
    'aliases' => array('paa'),
  );

  return $items;
}

function drush_patchfile_patch_status() {
  $projects = drush_patchfile_get_patched_projects();

  if (empty($projects)) {
    return drush_print(dt('No patches found.'));
  }

  $rows = array();
  $rows[] = array(dt('Project'), dt('Patch'), dt('Applied'));

  foreach ($projects as $project_name => $project_info) {
    $project_directory = drush_patchfile_project_get_directory($project_name);
    $project_extra = '';

    if (empty($project_directory)) {
      $project_extra .= ' ' . _drush_patchfile_color('red', dt('(missing)'));
    }

    foreach ($project_info as $patch) {
      $rows[] = array(
        $project_name . $project_extra,
        _drush_patchfile_patch_short_name($patch),
        drush_patchfile_is_patch_applied($patch, $project_directory, TRUE),
      );
    }
  }

  drush_print_table($rows, TRUE);
}

/**
 * Command callback for drush patch-apply-all
 */
function drush_patchfile_patch_apply_all() {
  $projects = drush_patchfile_get_patched_projects();

  if (empty($projects)) {
    return drush_print(dt('No patches found.'));
  }

  foreach ($projects as $project_name => $project_info) {
    drush_patchfile_patch_project($project_name);
  }
}

/**
 * Command callback for drush patch-project.
 *
 * @param string $project_name
 *   The project name to apply patches to from the manifest.
 */
function drush_patchfile_patch_project($project_name, $project_directory = NULL) {
  $patches = drush_patchfile_get_project_patches($project_name);
  if (empty($patches)) {
    return drush_set_error('DRUSH_PROJECT_NO_PATCHES', dt("There are no patches to apply to @project.", array('@project' => $project_name)));
  }

  if (!isset($project_directory)) {
    $project_directory = drush_patchfile_project_get_directory($project_name);
  }
  if (empty($project_directory)) {
    return drush_set_error('DRUSH_PROJECT_NOT_FOUND', dt("The project @project was not found.", array('@project' => $project_name)));
  }

  if (drush_get_option('select')) {
    $options = array('all' => dt('All')) + array_map('_drush_patchfile_patch_short_name', $patches);
    $result = drush_choice($options, dt("Which patch do you want to apply?"));
    if ($result === FALSE) {
      return FALSE;
    }
    elseif ($result != 'all') {
      $patches = array($patches[$result]);
    }
  }

  return _drush_patchfile_project_apply_patches($project_name, $patches, $project_directory);
}

/**
 * Command callback for drush patch-project.
 *
 * @param string $project_name
 *   The project name to apply patches to from the manifest.
 * @param string $patch_url
 *   The URL containing the patch file.
 */
function drush_patchfile_patch_add($project_name, $url) {
  $file = drush_get_option('patch-file');
  $patch = $url;

  $args = array(
    '@project' => $project_name,
    '@url' => $url,
    '@file' => $file,
  );

  $project_directory = drush_patchfile_project_get_directory($project_name);
  if (empty($project_directory)) {
    return drush_set_error('DRUSH_PROJECT_NOT_FOUND', dt("The project @project was not found.", $args));
  }

  _drush_patchfile_preprocess_local_file_url($patch, $file);

  // Check to see if this project is already patched with this patch.
  $projects = drush_patchfile_get_patched_projects();
  if (isset($projects[$project_name])) {
    if (in_array($patch, $projects[$project_name])) {
      return drush_set_error('DRUSH_PATCH_ALREADY_LISTED', dt("The patch @url is already listed as a patch for @project in @file.", $args));
    }
  }

  if (!drush_get_option('no-apply')) {
    if (_drush_patchfile_project_apply_patches($project_name, array($patch), $project_directory) || drush_get_option('force')) {
      _drush_patchfile_add_patch($project_name, $patch['url']);
    }
    else {
      if (drush_confirm("Would you still like to add this patch to the patch file?")) {
        _drush_patchfile_add_patch($project_name, $url, $file);
      }
      return FALSE;
    }
  }
  else {
    if (_make_download_file($patch['url']) || drush_get_option('force')) {
      _drush_patchfile_add_patch($project_name, $patch['url']);
    }
    else {
      return drush_set_error('DOWNLOAD_ERROR', dt('Unable to download or fetch @url.', $args));
    }
  }
}

/**
 * Implements hook_drush_pm_post_download().
 */
function patchfile_drush_pm_post_download($request, $release) {
/*
$request = array (
  'name' => 'search404',
  'version' => NULL,
  'drupal_version' => '7.x',
  'project_version' => NULL,
  'status url' => 'http://updates.drupal.org/release-history',
  'project_type' => 'module',
  'project_dir' => 'search404',
  'base_project_path' => 'search404',
  'full_project_path' => '/home/dave/CloudStation/projects/lullabot/lullabot.com/search404',
  'project_install_location' => '/home/dave/CloudStation/projects/lullabot/lullabot.com/search404',
)
 */

  if (drush_patchfile_get_project_patches($request['name'])) {
    //if (drush_confirm(dt("Patches detected for @project. Do you want to attempt to re-apply them?", array('@project' => $request['name'])))) {
      // @todo Should this skip the patch choice? If not, we should use drush_invoke_process()
      //drush_invoke_process('@self', 'patch-project', array($request['name']));
      $dir = $request['full_project_path'];
      $root = drush_get_context('DRUSH_DRUPAL_ROOT');
      if (!empty($root) && strpos($dir, $root) === FALSE) {
        //$dir = $root . '/' . $dir;
      }
      drush_patchfile_patch_project($request['name'], $dir);
    //}
  }
}

function _drush_patchfile_patch_short_name($patch) {
  $return = basename($patch['url']);
  $return = preg_replace('/\.(patch|diff)$/', '', $return);
  return $return;
}

function drush_patchfile_engine($file = NULL, array $config = array()) {
  if (!isset($file)) {
    $file = drush_get_option('patch-file');
  }
  if (empty($file)) {
    throw new Exception("No patch file provided");
  }

  if ($instance = drush_get_engine('patchfile')) {
    $instance->setFile($file);
    return $instance;
  }

  $engine = drush_get_option('patch-file-engine', pathinfo($file, PATHINFO_EXTENSION));
  if (empty($engine)) {
    throw new Exception("Unable to automatically determine the patch engine to use for $file");
  }

  if ($instance = drush_load_engine('patchfile', $engine, NULL, NULL, $config)) {
    $instance->setFile($file);
    return $instance;
  }
  else {
    throw new Exception("Unable to load patch engine $engine");
  }
}

function drush_patchfile_get_patched_projects() {
  return drush_patchfile_engine()->getProjectPatches();
}

function drush_patchfile_get_project_patches($project_name) {
  try {
    return drush_patchfile_engine()->getProjectPatches($project_name);
  }
  catch (Exception $e) {
    return array();
  }
}

/**
 * Return the directory of the project.
 *
 * @param string $project_name
 *   The name of the module or theme to lookup.
 *
 * @todo Switch this call to use drush_invoke_process('@self', 'drupal-directory', array($project_name))
 */
function drush_patchfile_project_get_directory($project_name) {
  $drupal_root = drush_get_context('DRUSH_DRUPAL_ROOT');

  if ($project_name == 'drupal') {
    return $drupal_root;
  }

  // Get all possible modules, themes, and profiles.
  drush_include_engine('drupal', 'environment');
  $projects = array_merge(drush_get_modules(), drush_get_themes());
  //$projects = array_merge($projects, drush_find_profiles($drupal_root));

  if (isset($projects[$project_name])) {
    return $drupal_root . '/' . dirname($projects[$project_name]->filename);
  }

  return FALSE;
}

/**
 * Test if a patch is currently applied to a directory.
 *
 * @param array $patch
 *   A patch array, must have 'url' value defined at minimum for
 *   _make_download_file().
 * @param string $directory
 *   The directory the patch should be applied to.
 * @param bool $nice_format
 *   Boolean if TRUE will return the status in a human-readable format.
 *
 * @return bool|null
 *   TRUE if the patch is currently applied. FALSE if the patch is not
 *   currently applied, but can be. NULL if we could not determine if
 *   the patch is applied or not (likely re-roll needed).
 */
function drush_patchfile_is_patch_applied(array $patch, $directory, $nice_format = FALSE) {
  $applied = NULL;

  if ($filename = _make_download_file($patch['url'])) {
    $result = DrushPatchFileGit::checkPatch($directory, $filename);
    $applied = $result['applied'];
    drush_op('unlink', $filename);
  }

  if (!$nice_format) {
    return $applied;
  }

  if ($applied === TRUE) {
    return _drush_patchfile_color('green', dt('Yes'));
  }
  elseif ($applied === FALSE) {
    return _drush_patchfile_color('red', dt('No'));
  }
  else {
    return _drush_patchfile_color('yellow', dt('Unsure'));
  }
}

function _drush_patchfile_color($color, $string) {
  $colors = array(
    'red' => 31,
    'green' => 32,
    'yellow' => 33,
    'blue' => 34,
  );

  if (drush_get_context('DRUSH_NOCOLOR') || !isset($colors[$color])) {
    return $string;
  }

  return sprintf("\033[1;%sm%s\033[0m", $colors[$color], $string);
}

/**
 * Copied from DrushMakeProject::preprocessLocalFileUrl().
 *
 * @see DrushMakeProject::preprocessLocalFileUrl()
 */
function _drush_patchfile_preprocess_local_file_url(&$info, $makefile) {
  if (is_string($info)) {
    $info = array('url' => $info, 'local' => FALSE);
  }

  if (!_drush_patchfile_is_url($info['url']) && !drush_is_absolute_path($info['url'])) {
    $info['url'] = dirname($makefile) . '/' . $info['url'];
    $info['local'] = TRUE;
  }
  elseif (substr($info['url'], 0, 8) == 'file:///') {
    $info['url'] = substr($info['url'], 7);
    $info['local'] = TRUE;
  }

  $patches = _drush_patchfile_get_possible_patches_from_url($info['url']);
  if ($patches !== FALSE) {
    if (empty($patches)) {
      throw new Exception("Unable to determine patch file for {$info['url']}.");
    }
    else {
      if ($patch = drush_choice($patches, dt("Which patch do you want to apply?"))) {
        $info['url'] = $patch;
      }
    }
  }
}

/**
 * A duplicate of _drush_is_url() so this will work on 5.x
 */
function _drush_patchfile_is_url($url) {
  return parse_url($url, PHP_URL_SCHEME) !== NULL;
}

/**
 * Add a patch record to a make file.
 *
 * @param string $project_name
 *   The project being patched.
 * @param string $patch_url
 *   The URL of the patch (remote or local).
 * @param string $file
 *   The make file to add the patch record to.
 *
 * @return bool
 *   TRUE if the file was appended successfully, or FALSE otherwise.
 */
function _drush_patchfile_add_patch($project_name, $patch_url) {
  return drush_patchfile_engine()->addPatch($project_name, $patch_url);
}

/**
 * Copied from DrushMakeProject::applyPatches().
 *
 * Sections commented out are code responsible for checksum verification and
 * writing PATCHES.txt files.
 *
 * @see DrushMakeProject::applyPatches()
 */
function _drush_patchfile_project_apply_patches($project_name, array $patches, $project_directory) {
  if (empty($patches)) {
    return TRUE;
  }

  $patches_txt = '';
  $local_patches = array();
  foreach ($patches as $info) {
    // $this->preprocessLocalFileUrl($info);

    // Download the patch.
    if ($filename = _make_download_file($info['url'])) {
      $patched = DrushPatchFileGit::applyPatch($project_directory, $filename);

      // Set up string placeholders to pass to dt().
      $dt_args = array(
        '@name' => $project_name,
        '@filename' => basename($filename),
      );

      drush_op('unlink', $filename);

      if ($patched) {
        // $patch_url = $info['url'];

        // // If this is a local patch, copy that into place as well.
        // if ($info['local']) {
        //   $local_patches[] = $info['url'];
        //   // Use a local path for the PATCHES.txt file.
        //   $pathinfo = pathinfo($patch_url);
        //   $patch_url = $pathinfo['basename'];
        // }
        // $patches_txt .= '- ' . $patch_url . "\n";

        drush_log(dt('@name patched with @filename.', $dt_args), 'success');
      }
      else {
        make_error('PATCH_ERROR', dt("Unable to patch @name with @filename.", $dt_args));
        return FALSE;
      }
    }
    else {
      drush_set_error('DOWNLOAD_ERROR', dt('Unable to download or fetch @patch.', array('@patch' => $info['url'])));
      return FALSE;
    }
  }
  // if (!empty($patches_txt) && !drush_get_option('no-patch-txt') && !file_exists($project_directory . '/PATCHES.txt')) {
  //   $patches_txt = "The following patches have been applied to this project:\n" .
  //     $patches_txt .
  //     "\nThis file was automatically generated by Drush Make (http://drupal.org/project/drush).";
  //   file_put_contents($project_directory . '/PATCHES.txt', $patches_txt);
  //   drush_log('Generated PATCHES.txt file for ' . $this->name, 'ok');

  //   // Copy local patches into place.
  //   foreach ($local_patches as $url) {
  //     $pathinfo = pathinfo($url);
  //     drush_copy_dir($url, $project_directory . '/' . $pathinfo['basename']);
  //   }
  // }
  return TRUE;
}

interface DrushPatchFileInterface {

  public function __construct(array $config = array());

  public function getProjectPatches($project_name = NULL);

  public function addPatch($project_name, $patch_url);
}

abstract class drush_patchfile implements DrushPatchFileInterface {
  protected $file = NULL;
  protected $config = array();

  public function __construct(array $config = array()) {
    $config += array('cache' => TRUE);
    $this->config = $config;
  }

  public function setFile($file) {
    if (!is_file($file)) {
      throw new Exception("The patch file $file does not exist.");
    }
    $this->file = $file;
  }

  public function useCache() {
    return !empty($this->config['cache']);
  }

  abstract public function getProjectPatches($project_name = NULL);

  abstract public function addPatch($project_name, $patch_url);
}

function _drush_patchfile_get_possible_patches_from_url($url) {
  if (preg_match('~^https?://(?:www\.)?drupal\.org(/node/\d+(#comment-\d+)?)~i', $url, $matches)) {
    $patches = array();
    if ($request = @file_get_contents($url)) {
      $dom = @new DOMDocument();
      @$dom->loadHTML($request);
      $xpath = new DOMXpath($dom);

      foreach ($xpath->query("//table[@id='extended-file-field-table-field-issue-files']/tbody/tr[not(contains(concat(' ',normalize-space(@class),' '),' element-hidden '))]") as $element) {
        $file_xpath = $xpath->query("td[@class='extended-file-field-table-filename']//a", $element);
        if (!$file_xpath->length) {
          continue;
        }

        // If a comment number was provided, then filter the results to only
        // patches that match the comment.
        $cid_query = "td[@class='extended-file-field-table-cid']//a";
        if (!empty($matches[2])) {
          $cid_query .= "[@href='{$matches[1]}']";
        }
        $cid_xpath = $xpath->query($cid_query, $element);
        if (!$cid_xpath->length) {
          continue;
        }
        $cid = ltrim($cid_xpath->item(0)->textContent, '#');

        $size = $xpath->query("td[@class='extended-file-field-table-filesize']", $element)->item(0)->textContent;
        $author = $xpath->query("td[@class='extended-file-field-table-uid']//a", $element)->item(0)->textContent;

        $patch_url = $file_xpath->item(0)->getAttribute('href');
        $patches[$patch_url] = array(
          'patch' => $file_xpath->item(0)->textContent,
          'details' =>  sprintf("%10s by %s on comment %d", $size, $author, $cid),
        );
      }
    }
    return $patches;
  }

  if (preg_match('~^https://github\.com/[\w-]+/[\w-]+/pull/\d+$~i', $url, $matches)) {
    if (@file_get_contents($url . '.patch')) {
      return array($url . '.patch' => $url);
    }
  }

  return FALSE;
}
