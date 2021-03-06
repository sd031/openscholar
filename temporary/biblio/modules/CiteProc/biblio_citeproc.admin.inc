<?php
function biblio_citeproc_style_manager_form($form, &$form_state) {
  $form = array();
  $options = array();
  $tree = array();

  $cache = cache_get('github_csl_repo');
  if (!$cache || $cache->expire < time()) {
    $treeURL = 'https://api.github.com/repos/citation-style-language/styles/contents';
    $result = drupal_http_request($treeURL);

    if ($result->code == 200) {
      $tree = json_decode($result->data);
    }

    foreach ($tree as $file) {
      if (strstr($file->name, '.csl')) {
        $options[$file->path] =  basename($file->name);
      }
    }

    if (!empty($options)) {
      $expire = time()+86400; //one day from now
      cache_set('github_csl_repo', $options, 'cache', $expire);
    }
  }
  else {
    $options = $cache->data;
  }


  $form['remote_names'] = array(
    '#type' => 'value',
    '#value' => $options,
  );

  asort($options);
  $form['avialable_styles'] = array(
    '#type' => 'select',
    '#title' => t('Available styles'),
    '#size' => 15,
    '#multiple' => TRUE,
    '#description' => t('Choose the styles you would like to download and install.'),
  );

  $form['install'] = array(
    '#type'        => 'submit',
    '#value'       => '<--',
    '#description' => t('Install the selected styles from GitHub'),
  );
  $form['remove'] = array(
    '#type'        => 'submit',
    '#value'       => '-->',
    '#description' => t('Un-install the selected styles'),
  );
  $form['default'] = array(
    '#type'  => 'submit',
    '#value' => t('Set as site default')
  );
  $form['edit'] = array(
    '#type'  => 'submit',
    '#value' => t('Edit selected')
  );
  $form['install_all'] = array(
    '#type'     => 'submit',
    '#value'    => t('Install all')
  );

  $form['#attributes']['enctype'] = 'multipart/form-data';

  $form['import_csl_file'] = array(
    '#type' => 'file',
    '#title' => t('Import Local CSL file'),
    '#default_value' => '',
    '#size' => 60
  );
  $form['import'] = array(
    '#type'   => 'submit',
    '#value'  => t('Import'),
    '#submit' => array('biblio_citeproc_csl_file_import_submit'),
  );

  $result = db_select('biblio_citeproc_styles', 'csl')
    ->fields('csl', array('filename', 'id', 'sha1', 'title'))
    ->orderBy('filename', 'ASC')
    ->execute();

  $titles = array();
  $installed = array();
  foreach ($result as $style) {
    $installed[$style->id] = $style->filename;
    $titles[$style->filename] = $style->title;

    if (($sha = array_search($style->filename, $options))) {
      unset($options[$sha]);
    }
  }
  $form['avialable_styles']['#options'] = $options;

  $form['installed_styles'] = array(
    '#type' => 'select',
    '#title' => t('Installed styles'),
    '#size' => 15,
    '#options' => biblio_get_styles(),
    '#multiple' => TRUE,
    '#description' => t('Currently installed styles.'),
  );

  $form['current_default'] = array(
    '#markup' => empty($titles) ? '' : $titles[variable_get('biblio_citeproc_style', 'cse.csl')],
  );

  return $form;
}
function theme_biblio_citeproc_style_manager_form($variables) {
  $form = $variables['form'];
  $rows = array();
  $rows[] = array(
    array('data' => t('Current default style')),
    array('data' => '<b>' . drupal_render($form['current_default']) . '</b>'),
  );
  $rows[] = array(
    array('data' => t('Example citation:')),
    array('data' => biblio_citeproc_example_citation()));
  $output = theme('table', array('rows' => $rows));
  $rows = array();
  $rows[] = array(
    array('data' => drupal_render($form['installed_styles']) . '<br>' . drupal_render($form['default']) . drupal_render($form['edit'])),
    array('data' => drupal_render($form['install']) . '<br>' . drupal_render($form['remove'])),
    array('data' => drupal_render($form['avialable_styles']) . '<br>' . drupal_render($form['install_all'])
    ),
  );
  $rows[] = array(array('data' => drupal_render($form['import_csl_file']) .  drupal_render($form['import']),'colspan' => 3) );
  $output .= theme('table', array('rows' => $rows));

  $output .= drupal_render_children($form);
  return $output;

}
function biblio_citeproc_style_manager_form_validate($form, &$form_state) {
  if ($form_state['clicked_button']['#value'] == '<--' && count( $form_state['values']['avialable_styles'])) {
    if (count($form_state['values']['avialable_styles']) > 60) {
      $message = t('You may not select more than 60 styles for installation at one time');
      form_error($form['avialable_styles'], $message);
    }
  }
  if ($form_state['clicked_button']['#value'] ==  t('Set as site default') && !count( $form_state['values']['installed_styles'])) {
    form_error($form['installed_styles'], t('You must select an installed style to set as the default.'));
  }

}
function biblio_citeproc_style_manager_form_submit($form, &$form_state) {
  if ($form_state['clicked_button']['#value'] == '<--' && count( $form_state['values']['avialable_styles'])) {
    $github_URL = 'https://api.github.com/repos/citation-style-language/styles/contents/';
    foreach ( $form_state['values']['avialable_styles'] as $path) {
      $URL = $github_URL . $path;
      $result = drupal_http_request($URL);
      if ($result->code == 200) {
        $file  = json_decode($result->data);
        switch ($file->encoding) {
          case 'base64':
            $csl = base64_decode($file->content);
            break;
        }
        _install_csl($file->name, $csl, $file->sha);
      }
    }
  }

  if ($form_state['clicked_button']['#value'] == '-->' && count( $form_state['values']['installed_styles'])) {
    foreach ($form_state['values']['installed_styles'] as $id) {
      db_delete('biblio_citeproc_styles')->condition('filename', $id)->execute();
    }
  }

  if ($form_state['clicked_button']['#value'] == t('Set as site default') &&
    count( $form_state['values']['installed_styles'])) {
    $def = array_shift($form_state['values']['installed_styles']);
    variable_set('biblio_citeproc_style', $def);
  }

  if ($form_state['clicked_button']['#value'] == t('Edit selected') &&
    count( $form_state['values']['installed_styles'])) {
    $style = array_shift($form_state['values']['installed_styles']);
    $dest = drupal_get_destination();
    drupal_goto('admin/config/content/biblio/citeproc/styles/' . $style . '/edit');
  }

  if ($form_state['clicked_button']['#value'] == t('Install all')) {
    $batch_op = array(
      'title' => t('Importing all styles from GitHub repository'),
      'operations' => array(
        array('_get_zip_from_github', array()),
        array('_install_from_zip', array()),
      ),
      'progressive' => TRUE,
      'finished' => '_csl_import_batch_finished',
      'init_message' => t('Downloading file...'),
      'progress_message' => t('Saving styles...'),
      'file' => './' . drupal_get_path('module', 'biblio_citeproc') . '/biblio_citeproc.admin.inc'
    );
    batch_set($batch_op);
    batch_process('admin/config/content/biblio/styles');

  }

  if ($form_state['clicked_button']['#value'] == t('Import')) {
  }
}

function biblio_citeproc_csl_file_import_submit($form, &$form_state) {
  $validators = array(
    'file_validate_extensions' => array('csl xml'),
    'biblio_citeproc_validate_csl_file' => array()
  );

  if ($import_file = file_save_upload('import_csl_file', $validators)) {
    $csl = file_get_contents($import_file->uri);
//    if (biblio_citeproc_validate_csl($csl)) {
    _install_csl($import_file->filename, $csl);
//    }
  }

}

function _install_csl($name, $csl, $sha = NULL, $all = FALSE) {
  static  $installed = array();

  if (empty($installed)) {
    $result = db_select('biblio_citeproc_styles', 'csl')
      ->fields('csl', array('filename', 'id', 'sha1', 'title'))
      ->orderBy('filename', 'ASC')
      ->execute();

    $installed = array();
    foreach ($result as $style) {
      $installed[$style->id] = $style;
    }
  }

  $xml = simplexml_load_string($csl);

  if ($xml) {
    $parent = '';
    foreach ($xml->info->link as $link) {
      $attrs = $link->attributes();
      if (isset($attrs['rel']) && $attrs['rel'] == 'independent-parent') {
        $parent = (string)$attrs['href'];
      }
    }
    if (!$all && !empty($parent)) {
      $csl_file_contents = db_query("SELECT csl FROM biblio_citeproc_styles WHERE id = :parent", array(':parent' => $parent))->fetchField();
      if (!$csl_file_contents) {
        drupal_set_message(t('You do not have the parent style file: !parent_id installed. You must install !parent_id before you can use !csl_id', array('!parent_id' => basename($parent), '!csl_id' => $name)), 'error');
      }
    }

    $sha1 = (isset($sha)) ? $sha : sha1($csl);

    $record  = array(
      'filename' => $name,
      'parent'   => $parent,
      'title'    => trim((string)$xml->info->title),
      'summary'  => (string)$xml->info->summary,
      'csl'      => $csl,
      'sha1'     => $sha1,
      'id'       => (string)$xml->info->id,
    );

    if (!array_key_exists($record['id'], $installed)) {
      db_insert('biblio_citeproc_styles')->fields($record)->execute();
      $installed[$record['id']] = TRUE;
    }
    elseif ($record['sha1'] != $installed[$record['id']]->sha1) {
      db_update('biblio_citeproc_styles')->condition('id', $record['id'])->fields($record)->execute();
    }
    elseif(($record['sha1'] == $installed[$record['id']]->sha1)) {
      $message = t('The CSL file you supplied: !name, is already installed', array('!name' => $name));
      drupal_set_message($message, 'warning');
    }
  }
  else {
    drupal_set_message(t('I could not parse the CSL provided as valid XML', 'error'));
  }
}

function _get_zip_from_github(&$context = NULL) {
  $zip_url = 'https://github.com/citation-style-language/styles/zipball/master';
  $zip_file = system_retrieve_file($zip_url);

  $filename = drupal_realpath($zip_file);
  if (!empty($filename)) {
    variable_set('github_zip', $filename);
  }
}
function _install_from_zip(&$context = NULL) {

  $destination = variable_get('github_zip', '');

  if (!empty($destination)) {
    $zip = zip_open($destination);
    $za = new ZipArchive();

    if ($za->open($destination) !== TRUE) {
      $message = t('Could not open zip file containing styles: !file', array('!file' => realpath($destination)));
      $message = check_plain($message);
      drupal_set_message($message, 'error');
      return;
    }
    if (empty($context['sandbox'])) {
      $context['sandbox']['progress'] = 0;
      $context['results']['install_count'] = 0;
    }

    $num_files = $za->numFiles;
    $start = $context['sandbox']['progress'];
    $end = min(($start+50), $num_files);

    for ($i = $start; $i < $end; $i++) {
      $name = $za->getNameIndex($i);
      $name = basename($name);
      if (strstr($name, '.csl')) {
        $csl = $za->getFromIndex($i);
        _install_csl($name, $csl, NULL, TRUE);
        $context['results']['install_count']++;
      }
      $context['sandbox']['progress']++;
    }
    $za->close();

    if ($context['sandbox']['progress'] != $num_files) {
      $context['finished'] = $context['sandbox']['progress'] / $num_files;
    }
  }
}

function _csl_import_batch_finished($success, $results, $operations) {
  $destination = variable_get('github_zip', '');
  file_unmanaged_delete($destination);
  variable_del('github_zip');
}
function biblio_citeproc_example_citation() {
  global $language;
  $contributors = array(
    0 => array(
      'lastname' => 'Oneauth',
      'firstname' => 'Joe',
      'initials' => 'A',
      'auth_category' => 1,
      'cid' => -1),
    1 => array(
      'lastname' => 'Twoauth',
      'firstname' => 'John',
      'initials' => 'B',
      'auth_category' => 1,
      'cid' => -2),
  );
  $node = new stdClass();
  $node->nid = -1;
  $node->title                  = 'This is a fantastic title.';
  $node->biblio_contributors    = $contributors;
  $node->biblio_type            = 102;
  $node->biblio_year            = 2010;
  $node->biblio_volume          = 1;
  $node->biblio_issue           = 2;
  $node->biblio_secondary_title = 'Journal of Fantastic Articles';
  $node->biblio_pages           = '	424-31';
  $node->biblio_coins           = '';
  return theme_biblio_citeproc_style(array('node' => $node));

}

function biblio_citeproc_csl_editor($form, &$form_state, $style) {

  $csl = db_query('SELECT id,parent,csl FROM {biblio_citeproc_styles} WHERE filename = :id', array(':id' => $style))->fetchObject();
  if (!isset($csl->csl)) {
    drupal_set_message(t('Biblio-CiteProc could not fetch the style file: !csl_id from the database. Check your CiteProc settings.', array('!csl_id' => $style)), 'error');
    return;
  }
  if (!empty($csl->parent)) {
    $csl = db_query("SELECT id,csl FROM biblio_citeproc_styles WHERE id = :id", array(':id' => $csl->parent))->fetchObject();

  }
  if (isset($csl->csl)) {
    $csl_file_contents = $csl->csl;
  }

  $form['editor'] = array(
    '#title'         => t('Editing %style', array('%style' => $style)),
    '#type'          => 'text_format',
    '#rows'          => 40,
    '#format'        => 'csl',
    '#default_value' => $csl_file_contents,
  );
  $form['save'] = array(
    '#value' => t('Save'),
    '#type'  => 'submit',
  );
  $form['cancel'] = array(
    '#value' => t('Cancel'),
    '#type'  => 'submit',
  );
  $form['style'] = array(
    '#value' => $style,
    '#type'  => 'hidden',
  );
  $form['id'] = array(
    '#value' => $csl->id,
    '#type'  => 'hidden',
  );

  return $form;
}
function biblio_citeproc_csl_editor_validate($form, &$form_state) {
  if ($form_state['triggering_element']['#value'] == t('Save')) {
    $csl = $form_state['values']['editor']['value'];
    $valid = biblio_citeproc_validate_csl($csl);
    if (!empty($valid)) {
      form_set_error('editor', $valid[0]);
    }
    else {
      $form_state['values']['editor']['value'] = $csl;
    }
  }
}
function biblio_citeproc_csl_editor_submit($form, &$form_state) {
  $form_state['redirect'] = 'admin/config/content/biblio/citeproc/styles';

  if ($form_state['triggering_element']['#value'] == t('Save')) {
    $record = array(
      'id'  => $form_state['values']['id'],
      'csl' => $form_state['values']['editor']['value'],
    );
    drupal_write_record('biblio_citeproc_styles', $record, 'id');
  }

}

function biblio_citeproc_validate_csl_file($file) {
  if ($file->source == 'import_csl_file') {
    $csl = file_get_contents($file->uri);
    return biblio_citeproc_validate_csl($csl);
  }
}

function biblio_citeproc_validate_csl($csl) {
  $rng_schema = drupal_get_path('module', 'biblio_citeproc') .'/schema/csl.rng';
  $doc = new DOMDocument();
  $doc->loadXML($csl);
  $updated = $doc->getElementsByTagName('updated')->item(0);
  $updated->nodeValue = date(DATE_ATOM, time());
  $valid = $doc->relaxNGValidate($rng_schema);
  $csl = $doc->saveXML();
  return ($valid) ? array() : array(t('The supplied CSL file did not pass CSL 1.0 validation'));

}