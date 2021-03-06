<?php
/**
 * @file
 *   Specific functions for a drupal 8+ environment.
 *   drush_include_engine() magically includes either this file
 *   or environment_X.inc depending on which version of drupal Drush
 *   is called from.
 */

use Drupal\Core\Site\Settings;
use Drupal\Core\Logger\RfcLogLevel;

/**
 * Get complete information for all available modules.
 *
 * @param $include_hidden
 *   Boolean to indicate whether hidden modules should be excluded or not.
 * @return
 *   An array containing module info for all available modules.
 */
function drush_get_modules($include_hidden = TRUE) {
  $modules = system_rebuild_module_data();

  foreach ($modules as $key => $module) {
    if ((!$include_hidden) && (isset($module->info['hidden']))) {
      unset($modules[$key]);
    }
    else {
      $module->schema_version = drupal_get_installed_schema_version($key);
    }
  }

  return $modules;
}

/**
 * Returns drupal required modules, including modules declared as required dynamically.
 */
function _drush_drupal_required_modules($module_info) {
  $required = drupal_required_modules();
  foreach ($module_info as $name => $module) {
    if (isset($module->info['required']) && $module->info['required']) {
      $required[] = $name;
    }
  }
  return array_unique($required);
}

/**
 * Return dependencies and its status for modules.
 *
 * @param $modules
 *   Array of module names
 * @param $module_info
 *   Drupal 'files' array for modules as returned by drush_get_modules().
 * @return
 *   Array with dependencies and status for $modules
 */
function drush_check_module_dependencies($modules, $module_info) {
  $status = array();
  foreach ($modules as $key => $module) {
    $dependencies = array_reverse($module_info[$module]->requires);
    $unmet_dependencies = array_diff(array_keys($dependencies), array_keys($module_info));
    if (!empty($unmet_dependencies)) {
      $status[$key]['error'] = array(
          'code' => 'DRUSH_PM_ENABLE_DEPENDENCY_NOT_FOUND',
          'message' => dt('Module !module cannot be enabled because it depends on the following modules which could not be found: !unmet_dependencies', array('!module' => $module, '!unmet_dependencies' => implode(',', $unmet_dependencies)))
      );
    }
    else {
      // check for version incompatibility
      foreach ($dependencies as $dependency_name => $v) {
        $current_version = $module_info[$dependency_name]->info['version'];
        $current_version = str_replace(drush_get_drupal_core_compatibility() . '-', '', $current_version);
        $incompatibility = drupal_check_incompatibility($v, $current_version);
        if (isset($incompatibility)) {
          $status[$key]['error'] = array(
            'code' => 'DRUSH_PM_ENABLE_DEPENDENCY_VERSION_MISMATCH',
            'message' => dt('Module !module cannot be enabled because it depends on !dependency !required_version but !current_version is available', array('!module' => $module, '!dependency' => $dependency_name, '!required_version' => $incompatibility, '!current_version' => $current_version))
          );
        }
      }
    }
    $status[$key]['unmet-dependencies'] = $unmet_dependencies;
    $status[$key]['dependencies'] = array_keys($dependencies);
  }

  return $status;
}

/**
 * Return dependents of modules.
 *
 * @param $modules
 *   Array of module names
 * @param $module_info
 *   Drupal 'files' array for modules as returned by drush_get_modules().
 * @return
 *   Array with dependents for each one of $modules
 */
function drush_module_dependents($modules, $module_info) {
  $dependents = array();
  foreach ($modules as $module) {
    $keys = array_keys($module_info[$module]->required_by);
    $dependents = array_merge($dependents, array_combine($keys, $keys));
  }

  return array_unique($dependents);
}

/**
 * Returns a list of enabled modules.
 *
 * This is a wrapper for module_list().
 */
function drush_module_list() {
  $modules = array_keys(\Drupal::moduleHandler()->getModuleList());
  return array_combine($modules, $modules);
}

/**
 * Installs a given list of modules.
 *
 * @see \Drupal\Core\Extension\ModuleHandlerInterface::install()
 *
 */
function drush_module_install($module_list, $enable_dependencies = TRUE) {
  return \Drupal::moduleHandler()->install($module_list, $enable_dependencies);
}

/**
 * Checks that a given module exists and is enabled.
 *
 * @see \Drupal\Core\Extension\ModuleHandlerInterface::moduleExists()
 *
 */
function drush_module_exists($module) {
  return \Drupal::moduleHandler()->moduleExists($module);
}

/**
 * Determines which modules are implementing a hook.
 *
 * @param string $hook
 *   The hook name.
 * @param bool $sort
 *  Not used in Drupal 8 environment.
 * @param bool $reset
 *  TRUE to reset the hook implementation cache.
 *
 * @see \Drupal\Core\Extension\ModuleHandlerInterface::getImplementations().
 * @see \Drupal\Core\Extension\ModuleHandlerInterface::resetImplementations().
 *
 */
function drush_module_implements($hook, $sort = FALSE, $reset = FALSE) {
  // $sort is there for consistency, but looks like Drupal 8 has no equilavient for it.
  // We can sort the list manually later if really needed.
  if ($reset == TRUE){
    \Drupal::moduleHandler()->resetImplementations();
  }
  return \Drupal::moduleHandler()->getImplementations($hook);
}

/**
 * Return a list of modules from a list of named modules.
 * Both enabled and disabled/uninstalled modules are returned.
 */
function drush_get_named_extensions_list($extensions) {
  $result = array();
  $modules = drush_get_modules();
  foreach($modules as $name => $module) {
    if (in_array($name, $extensions)) {
      $result[$name] = $module;
    }
  }
  $themes = drush_get_themes();
  foreach($themes as $name => $theme) {
    if (in_array($name, $extensions)) {
      $result[$name] = $theme;
    }
  }
  return $result;
}

/**
 * Enable a list of modules. It is assumed the list contains all the dependencies not already enabled.
 *
 * @param $modules
 *   Array of module names
 */
function drush_module_enable($modules) {
  // The list of modules already have all the dependencies, but they might not
  // be in the correct order. Still pass $enable_dependencies = TRUE so that
  // Drupal will enable the modules in the correct order.
  drush_module_install($modules);

  // Our logger got blown away during the container rebuild above.
  \Drupal::getContainer()->get('logger.factory')->addLogger(new \Drush\Log\DrushLog);

  // Flush all caches.
  drupal_flush_all_caches();
}

/**
 * Disable a list of modules. It is assumed the list contains all dependents not already disabled.
 *
 * @param $modules
 *   Array of module names
 */
function drush_module_disable($modules) {
  drush_set_error('DRUSH_MODULE_DISABLE', dt('Drupal 8 does not support disabling modules. Use pm-uninstall instead.'));
}

/**
 * Uninstall a list of modules.
 *
 * @param $modules
 *   Array of module names
 */
function drush_module_uninstall($modules) {
  module_uninstall($modules);
  // Our logger got blown away during the container rebuild above.
 \Drupal::getContainer()->get('logger.factory')->addLogger(new \Drush\Log\DrushLog);
}

/**
  * Invokes a hook in a particular module.
  *
  */
function drush_module_invoke($module, $hook) {
  $args = func_get_args();
  // Remove $module and $hook from the arguments.
  unset($args[0], $args[1]);
  return \Drupal::moduleHandler()->invoke($module, $hook, $args);
}

/**
  * Invokes a hook in all enabled modules that implement it.
  *
  */
function drush_module_invoke_all($hook) {
  $args = func_get_args();
  // Remove $hook from the arguments.
  array_shift($args);
  return Drupal::moduleHandler()->invokeAll($hook, $args);
}

/**
 * Get complete information for all available themes.
 *
 * @param $include_hidden
 *   Boolean to indicate whether hidden themes should be excluded or not.
 * @return
 *   An array containing theme info for all available themes.
 */
function drush_get_themes($include_hidden = TRUE) {
  $themes = \Drupal::service('theme_handler')->rebuildThemeData();
  foreach ($themes as $key => $theme) {
    if (!$include_hidden) {
      if (isset($theme->info['hidden'])) {
        unset($themes[$key]);
      }
    }
  }

  return $themes;
}

/**
 * Enable a list of themes.
 *
 * @param $themes
 *  Array of theme names.
 */
function drush_theme_enable($themes) {
  \Drupal::service('theme_handler')->install($themes);
}

/**
 * Disable a list of themes.
 *
 * @param $themes
 *  Array of theme names.
 */
function drush_theme_disable($themes) {
  drush_set_error('DRUSH_THEME_DISABLE', dt('Drupal 8 does not support disabling themes. Use pm-uninstall instead.'));
}

/**
 * Helper function to obtain the severity levels based on Drupal version.
 *
 * This is a copy of watchdog_severity_levels() without t().
 *
 * Severity levels, as defined in RFC 3164: http://www.ietf.org/rfc/rfc3164.txt.
 *
 * @return
 *   Array of watchdog severity levels.
 */
function drush_watchdog_severity_levels() {
  return array(
    RfcLogLevel::EMERGENCY => 'emergency',
    RfcLogLevel::ALERT => 'alert',
    RfcLogLevel::CRITICAL => 'critical',
    RfcLogLevel::ERROR    => 'error',
    RfcLogLevel::WARNING  => 'warning',
    RfcLogLevel::NOTICE   => 'notice',
    RfcLogLevel::INFO     => 'info',
    RfcLogLevel::DEBUG    => 'debug',
  );
}

/**
 * Helper function to obtain the message types based on drupal version.
 *
 * @return
 *   Array of watchdog message types.
 */
function drush_watchdog_message_types() {
  return _dblog_get_message_types();
}

function _drush_theme_default() {
  return \Drupal::config('system.theme')->get('default');
}

function _drush_theme_admin() {
  $theme = \Drupal::config('system.theme')->get('admin');
  return empty($theme) ? 'seven' : $theme;
}

function _drush_file_public_path() {
  return Settings::get('file_public_path', conf_path() . '/files');
}

function _drush_file_private_path() {
  return \Drupal::config('system.file')->get('path.private');
}

/**
 * Gets the extension name.
 *
 * @param $info
 *   The extension info.
 * @return string
 *   The extension name.
 */
function _drush_extension_get_name($info) {
  return $info->getName();
}

/**
 * Gets the extension type.
 *
 * @param $info
 *   The extension info.
 * @return string
 *   The extension type.
 */
function _drush_extension_get_type($info) {
  return $info->getType();
}

/**
 * Gets the extension path.
 *
 * @param $info
 *   The extension info.
 * @return string
 *   The extension path.
 */
function _drush_extension_get_path($info) {
  return $info->getPath();
}

/*
 * Wrapper for CSRF token generation.
 */
function drush_get_token($value = NULL) {
  return \Drupal::csrfToken()->get($value);
}

/*
 * Wrapper for _url().
 */
function drush_url($path = NULL, array $options = array()) {
  return \Drupal::urlGenerator()->generateFromPath($path, $options);
}
