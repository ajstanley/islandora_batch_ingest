<?php

/**
 * batch creation form submit
 * @global user $user
 * @param array $form
 * @param array $form_state
 * @param array $content_models
 */
function batch_creation_form(&$form_state, $collection_pid) {
  module_load_include('inc', 'fedora_repository', 'api/fedora_utils');
  module_load_include('inc', 'fedora_repository', 'CollectionPolicy');
  $policy = CollectionPolicy::loadFromCollection($collection_pid, TRUE);
  if (!$policy) {
    $form['titlebox'] = array(
      '#type' => 'item',
      '#value' => t("This collection is missing a Collection Policy"),
    );

    return $form;
  }

  $content_models = $policy->getContentModels();

  $cm_options = array();
  $name_mappings = array();
  foreach ($content_models as $content_model) {
    if ($content_model->pid != "islandora:collectionCModel") {
      $cm_options[$content_model->pid] = $content_model->name;
      $name_mappings[] = $content_model->pid . '^' . $content_model->pid_namespace;
    }
  }

  $mappings = implode('~~~', $name_mappings);
  $form['#attributes']['enctype'] = 'multipart/form-data';
  $form['link'] = array(
    '#type' => 'item',
    '#value' => l(t("Return to collection view"), "fedora/repository/$collection_pid"),
  );
  $form['titlebox'] = array(
    '#type' => 'item',
    '#value' => t("Batch ingest into $collection_pid"),
  );

  $form['collection_pid'] = array(
    '#type' => 'hidden',
    '#value' => $collection_pid,
  );
  $form['namespace_mappings'] = array(
    '#type' => 'hidden',
    '#value' => $mappings,
  );
  $form['content_model'] = array(
    '#title' => "Choose content model to be associated with objects ingested",
    '#type' => 'select',
    '#options' => $cm_options,
    '#required' => TRUE,
    '#description' => t("Content models describe the behaviours of objects with which they are associated."),
  );
  $form['indicator']['file-location'] = array(
    '#type' => 'file',
    '#title' => t('Upload zipped folder'),
    '#size' => 48,
    '#description' => t('The zipped folder should contain all files necessary to build objects.<br .>
      Related files must have the same filename, but with differing extensions to indicate mimetypes.<br />
      ie. <em>myFile.xml</em> and <em>myFile.jpg</em>'),
  );

  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Create Objects ')
  );


  return($form);
}

/**
 * @param array $form
 * @param array $form_state
 */
function batch_creation_form_validate($form, &$form_state) {

  $field_name = 'file-location';
  if (isset($_FILES['files']) && is_uploaded_file($_FILES['files']['tmp_name'][$field_name])) {
    $file = file_save_upload($field_name);
    if ($file->filemime != 'application/zip') {
      form_set_error($field_name, 'Input file must be a .zip file');
      return;
    }
    if (!$file) {
      form_set_error($field_name, 'Error uploading file.');
      return;
    }
    $form_state['values']['file'] = $file;
  }
  else {
    // set error
    form_set_error($field_name, 'Error uploading file.');
    return;
  }
}

function batch_creation_form_submit($form, &$form_state) {
  module_load_include('inc', 'fedora_repository', 'ContentModel');
  module_load_include('inc', 'fedora_repository', 'api/fedora_item');
  global $user;
  $namespace_mappings = array();
  $content_model = $form_state['values']['content_model'];
  $metadata = $form_state['values']['metadata_type'];
  $collection_pid = $form_state['values']['collection_pid'];
  $namespace_process = explode('~~~', $form_state['values']['namespace_mappings']);
  foreach ($namespace_process as $line) {
    $line_parts = explode('^', $line);
    $namespace_mappings[$line_parts[0]] = $line_parts[1];
  }
  $namespace = $namespace_mappings[$content_model];
  $namespace = preg_replace('/:.*/', '', $namespace);
  $dir_name = "temp" . $user->uid;
  $tmp_dir = file_directory_path() . '/' . $dir_name . '/';
  if (is_dir($tmp_dir)) {
    recursive_directory_delete($tmp_dir);
  }
  mkdir($tmp_dir);
  $file = $form_state['values']['file'];
  $fileName = $file->filepath;
  $file_list = array();
  $cmdString = "unzip -q -o -d $tmp_dir \"$fileName\"";
  system($cmdString, $retVal);
  $dirs = array();
  $do_not_add = array('.', '..', '__MACOSX');
  array_push($dirs, $tmp_dir);
  $files = scandir($tmp_dir);
  foreach ($files as $file) {
    if (is_dir("$tmp_dir/$file") & !in_array($file, $do_not_add)) {
      array_push($dirs, $tmp_dir . $file);
    }
  }
  foreach ($dirs as $directory) {
    if ($inputs = opendir($directory)) {
      while (FALSE !== ($file_name = readdir($inputs))) {
        if (!in_array($file_name, $do_not_add) && is_dir($file_name) == FALSE) {
          $ext = strrchr($file_name, '.');
          $base = preg_replace("/$ext$/", '', $file_name);
          $ext = substr($ext, 1);
          if ($ext) {
            if ($directory[strlen($directory) - 1] != '/') {
              $directory .= '/';
            }
            $file_list[$base][$ext] = "$directory" . $file_name;
          }
        }
      }
      closedir($inputs);
    }
  }

  if (($cm = ContentModel::loadFromModel($content_model, 'ISLANDORACM')) === FALSE) {
    drupal_set_message("$content_model not found", "error");
    return;
  }
  $item = new Fedora_Item($collection_pid);
  $policy = NULL;
  if (array_key_exists('CHILD_SECURITY', $item->datastreams)) {
    $policy = $item->get_datastream_dissemination('CHILD_SECURITY');
  }
  $batch = array(
    'title' => 'Ingesting Objects',
    'operations' => array(),
    'file' => drupal_get_path('module', 'islandora_batch_ingest') . '/BatchIngest.inc',
  );


  foreach ($file_list as $label => $object_files) {
    $batch['operations'][] = array('create_batch_objects', array($label, $content_model, $object_files, $collection_pid, $namespace, $metadata, $policy));
  }
  $batch['operations'][] = array('recursive_directory_delete', array($tmp_dir));
  batch_set($batch);
  batch_process("fedora/repository/$collection_pid");
}

/**
 *
 * @param <string> $label
 * @param <string> $content_model
 * @param <array> $object_files
 * @param <string> $collection_pid
 * @param <string> $namespace
 * @param <string> $metadata
 */
function create_batch_objects($label, $content_model, $object_files, $collection_pid, $namespace, $metadata, $policy) {
  module_load_include('inc', 'fedora_repository', 'ContentModel');
  module_load_include('inc', 'fedora_repository', 'MimeClass');
  module_load_include('inc', 'fedora_repository', 'api/fedora_item');
  module_load_include('inc', 'fedora_repository', 'api/dublin_core');
  global $user;
  $cm = ContentModel::loadFromModel($content_model, 'ISLANDORACM');
  $allowed_mime_types = $cm->getMimetypes();
  $mime_helper = new MimeClass();
  $pid = fedora_item::get_next_PID_in_namespace($namespace);
  $owner = ($user->uid > 0) ? $user->name : 'anonymous';

  $item = Fedora_item::ingest_new_item($pid, 'A', $label, $owner);
  $item->add_relationship('hasModel', $content_model, FEDORA_MODEL_URI);
  $item->add_relationship('isMemberOfCollection', $collection_pid);
  if ($object_files['xml']) {
    $data = file_get_contents($object_files['xml']);
    $xml = simplexml_load_string($data);
    $identifier = $xml->getName();
    if ($identifier == 'dc') {
      $item->modify_datastream_by_value($data, 'DC', "Dublin Core", 'text/xml', 'X');
      $mods_stream = batch_create_mods_from_dc($data);
      $item->add_datastream_from_string($mods_stream, 'MODS', 'MODS', 'text/xml', 'X');
    }
    if ($identifier == 'mods') {
      $mods_stream = $data;
      $item->add_datastream_from_string($mods_stream, 'MODS', 'MODS', 'text/xml', 'X');
      $dc_xml = batch_create_dc_from_mods($mods_stream);
      $item->modify_datastream_by_value($dc_xml, 'DC', "Dublin Core", 'text/xml', 'X');
    }

    $dc = new dublin_core($item);
    $titles = $dc->dc['dc:title'];
    $title = $titles[0];
    $item->modify_object($title);
  }
  else {
    $dc = new dublin_core($item);
    $dc_stream = $dc->as_xml();
    $mods_stream = batch_create_mods_from_dc($dc_stream);
    $item->add_datastream_from_string($mods_stream, 'MODS', 'MODS', 'text/xml', 'X');
  }
  if ($policy) {
    $item->add_datastream_from_string($policy, 'POLICY');
  }
  unset($object_files['xml']);
  foreach ($object_files as $ext => $filename) {
    $file_mimetype = $mime_helper->get_mimetype($filename);
    if (in_array($file_mimetype, $allowed_mime_types)) {
      $filename = preg_replace('/\/\//', '/', $filename);
      $added = $cm->execIngestRules($filename, $file_mimetype);
    }
    else {
      $item->purge("$pid $label not ingested.  $file_mimetype not permitted in objects associated with $content_model");
      drupal_set_message("$pid $label not ingested.  $file_mimetype not permitted in objects associated with $content_model");
      continue;
    }
    $item->add_datastream_from_file($filename, "OBJ");

    if (!empty($_SESSION['fedora_ingest_files'])) {
      foreach ($_SESSION['fedora_ingest_files'] as $dsid => $datastream_file) {
        $parts = pathinfo($datastream_file);
        $item->add_datastream_from_file($datastream_file, $datastream_id, $parts['basename']);
      }
    }
  }
}

/**
 * transforms mods to dc
 * @param string $mods_xml
 * @return string
 */
function batch_create_dc_from_mods($xmlstr) {
  $path = drupal_get_path('module', 'islandora_batch_ingest');
  module_load_include('inc', 'fedora_repository', 'ObjectHelper');
  module_load_include('inc', 'fedora_repository', 'CollectionClass');

  if ($xmlstr == NULL || strlen($xmlstr) < 5) {
    return " ";
  }

  try {
    $proc = new XsltProcessor();
  } catch (Exception $e) {
    drupal_set_message(t('@e', array('@e' => check_plain($e->getMessage()))), 'error');
    return " ";
  }

  $xsl = new DomDocument();
  $xsl->load($path . '/xslt/mods_to_dc.xsl');
  $input = new DomDocument();
  $input->loadXML(trim($xmlstr));
  $xsl = $proc->importStylesheet($xsl);
  $newdom = $proc->transformToDoc($input);
  $dc_xml = $newdom->saveXML();

  return $dc_xml;
}

/**
 * transforms dc to mods
 * @param string $mods_xml
 * @return string
 */
function batch_create_mods_from_dc($xmlstr) {
  $path = drupal_get_path('module', 'islandora_batch_ingest');
  module_load_include('inc', 'fedora_repository', 'ObjectHelper');
  module_load_include('inc', 'fedora_repository', 'CollectionClass');

  if ($xmlstr == NULL || strlen($xmlstr) < 5) {
    return " ";
  }

  try {
    $proc = new XsltProcessor();
  } catch (Exception $e) {
    drupal_set_message(t('@e', array('@e' => check_plain($e->getMessage()))), 'error');
    return " ";
  }

  $xsl = new DomDocument();
  $xsl->load($path . '/xslt/dc_to_mods.xsl');
  $input = new DomDocument();
  $input->loadXML(trim($xmlstr));
  $xsl = $proc->importStylesheet($xsl);
  $newdom = $proc->transformToDoc($input);
  $mods_xml = $newdom->saveXML();

  return $mods_xml;
}

/**
 *
 * @param <string> $dir
 * @return <boolean>
 */
function recursive_directory_delete($dir) {
  if (!file_exists($dir))
    return TRUE;
  if (!is_dir($dir))
    return unlink($dir);
  foreach (scandir($dir) as $item) {
    if ($item == '.' || $item == '..')
      continue;
    if (!recursive_directory_delete($dir . DIRECTORY_SEPARATOR . $item))
      return FALSE;
  }
  return rmdir($dir);
}