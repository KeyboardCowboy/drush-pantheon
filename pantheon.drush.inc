<?php
/**
 * @file
 * Drush wrapper for Pantheon's terminus commands.
 */

use \Drush\Log\LogLevel;

/**
 * Implements hook_drush_command().
 */
function pantheon_drush_command() {
  $items['pantheon-get-database'] = [
    'description' => "Replace the local database with one from a Pantheon environment.",
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_CONFIGURATION,
    'aliases' => ['pan-getdb', 'pgdb'],
    'arguments' => [
      'site' => dt('The Pantheon site name.'),
      'env' => dt('The Pantheon environment to fetch from. (dev, test, live)'),
    ],
    'required-arguments' => TRUE,
  ];

  return $items;
}

/**
 * Command validator.
 *
 * Check that terminus is installed, the environment exists, and that we can
 * connect to it.
 *
 * @param string $site
 *   The Pantheon site name.
 * @param string $env
 *   The Pantheon ENV name.
 *
 * @return null
 *   If there is an error in the validation.
 */
function drush_pantheon_get_database_validate($site, $env) {
  $valid_envs = ['dev', 'test', 'live'];

  // Check for terminus.
  if (!drush_shell_exec('terminus -V')) {
    return drush_set_error('PANTHEON_TERMINUS_NOT_FOUND', dt("Pantheon's command line tool, Terminus, is required but was not found.  Follow the instructions here to install Terminus: https://pantheon.io/docs/terminus/install/"));
  }

  // Verify that we have access to the site and environment specified.
  if (!drush_shell_exec('terminus site:info ' . $site)) {
    return drush_set_error('PANTHEON_SITE_NOT_ACCESSIBLE', dt("You don't appear to have access to the site !site.  Make sure you've authenticated with Pantheon and have access to this site.  See https://pantheon.io/docs/terminus/install/ for more info.", ['!site' => $site]));
  }

  // Verify that the environment is allowed.
  if (!in_array($env, $valid_envs)) {
    return drush_set_error('PANTHEON_INVALID_ENV', dt("Environment !env is not valid.  Pantheon environments are [" . implode('|', $valid_envs) . '].', ['!env' => $env]));
  }
}

/**
 * Command callback.
 *
 * Run the pantheon-get-database command.
 *
 * @param string $site
 *   The Pantheon site name.
 * @param string $env
 *   The Pantheon ENV name.
 */
function drush_pantheon_get_database($site, $env) {
  // Initialize variables.
  $pantheon_site_env = "{$site}.{$env}";
  $db_dest = drush_find_tmp() . '/' . $pantheon_site_env . '-' . time() . '.sql.gz';
  $command_terminus_create_backup = 'terminus backup:create ' . $pantheon_site_env . ' --element=db --keep-for=1 -y';
  $command_wget_database = 'wget --no-check-certificate -O ' . $db_dest . ' `terminus backup:get ' . $pantheon_site_env . ' --element=db -y`';

  // Get database information.
  drush_sql_bootstrap_further();
  $sql = drush_sql_get_class();
  $db_spec = $sql->db_spec();
  $db_name = $db_spec['database'];

  // Sanity check that we found a DB name.
  if (empty($db_name)) {
    return drush_log(dt("Unable to locate local database. Make sure your settings.local.php file is properly configured."), LogLevel::ERROR);
  }

  // Define some vars for the translations.
  $t_args = [
    '!source' => $pantheon_site_env,
    '!db' => $db_name,
    '!dest' => $db_dest,
  ];

  // Make sure the user's cool with this.
  if (!drush_confirm(dt('This will wipe out the local database !db and replace it with the contents of !source. Continue?', $t_args))) {
    return drush_user_abort();
  }

  // Tell Pantheon to create a backup.
  drush_log(dt("Creating backup of !source...", $t_args), LogLevel::OK);
  if (!drush_shell_exec($command_terminus_create_backup)) {
    return drush_set_error('PANTHEON_BACKUP_FAILED', dt('Error creating a backup on Pantheon.'));
  }

  // Fetch the backup from Pantheon.
  drush_log(dt("Downloading database..."), LogLevel::OK);
  if (!drush_shell_exec($command_wget_database)) {
    return drush_set_error('PANTHEON_DOWNLOAD_FAILED', dt('Error fetching the backup from Pantheon.'));
  }

  // We successfully snagged a DB from Pantheon.  Now check the local setup and
  // import.
  if (file_exists($db_dest)) {
    $sql->drop_or_create();

    drush_log(dt('Importing database into !db...', $t_args), LogLevel::OK);
    if (_pantheon_import_database($db_dest)) {
      drush_log(dt("Database imported from !source into !db!", $t_args), LogLevel::SUCCESS);
    }
    else {
      drush_set_error('PANTHEON_IMPORT_FAILED', dt("Error importing database."));
    }
  }
  else {
    drush_set_error('PANTHEON_DOWNLOAD_FAILED', dt("Failed to download database from !source to !dest", $t_args));
  }
}

/**
 * Import the database from the downloaded file into the local DB.
 *
 * @param string $db_dest
 *   Path to the downloaded file.
 *
 * @return bool
 *   TRUE if the database query ran successfully.
 */
function _pantheon_import_database($db_dest) {
  // @todo drush_redispatch_get_options() assumes you will execute same command. Not good.
  $global_options = drush_redispatch_get_options() + ['strict' => 0];

  // We do not want to include root or uri here.  If the user
  // provided -r or -l, their key has already been remapped to
  // 'root' or 'uri' by the time we get here.
  unset($global_options['root']);
  unset($global_options['uri']);

  $options = $global_options + [
    'file' => $db_dest,
    'file-delete' => TRUE,
  ];

  $return = drush_invoke_process(['@self'], 'sql-query', [], $options);
  if ($return['error_status']) {
    // An error was already logged.
    return FALSE;
  }
  else {
    return TRUE;
  }
}
