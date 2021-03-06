<?php

function asu_rfi_undergrad_post($data)
{
  $nodeid = $data['submission_nodeid'];

  $node = node_load($nodeid);
  
  $nodeArr = array(
    $node->field_rfi_program['und'][0]['value'] => $node->field_rfi_program_name['und'][0]['value']
  );
  $availableCampuses = asu_rfi_query_codes($nodeArr);

  $node_wrapper = entity_metadata_wrapper('node', $node);

  if ($node_wrapper->field_rfi_submission_status->value() == "Incomplete") {

    $node_wrapper->field_rfi_submission_status->set('Pending');
    $node_wrapper->save();

  } else if ($node_wrapper->field_rfi_submission_status->value() == "Pending") {

    $node_wrapper->field_rfi_submission_status->set('action_required');
    $node_wrapper->save();

  }

  $mw_auth_key = variable_get('asu_rfi_form_auth_key');
  $fname = $data['first_name'];
  $lname = $data['last_name'];
  $email = $data['email'];
  $ipAddress = $data['ip_address'];
  $phone = preg_replace('[\D]', '', $data['phone']);
  if (!empty($data['start_date'])) {
    $term = $data['start_date'];
  } else {
    $term = '';
  }

  $location = $data['location'];
  $campus = $data['campus'];
  $state = $data['state'];
  $prog_code = $data['program_code'];
  if ($prog_code == "" || empty($prog_code)) {
    $prog_code = "Exploratory";
    $college_code = null;
  } else {
    //Get college code from database based on program code
    $college_code = db_query('select field_asu_academic_program_value from field_data_field_asu_academic_program prog join field_data_field_asu_academic_plan plan on prog.entity_id = plan.entity_id where plan.field_asu_academic_plan_value = :plancode', array(':plancode' => $prog_code))->fetchField();
  }
  $form_mode = variable_get('asu_rfi_form_mode');
  $hashkey = sha1($mw_auth_key . '+' . $nodeid);
  $type_of_form = $data['type_of_form'];
  $domain = isset($data['domain']) ? $data['domain'] : $_SERVER['HTTP_HOST'];;
  $source = $domain . '-' . $type_of_form . '-UG';

  $campus = asu_rfi_correct_campus_idu($campus);

  //mobile phone
  if (isset($data['mobile_check'])) {
    $mobile = str_replace('-', '', $data['mobile_check']);
  } else {
    $mobile = '';
  }

  //date of birth
  if (isset($data['birthdate'])) {
    $dob = $data['birthdate'];
  } else {
    $dob = '';
  }

  //Military
  if (isset($data['military'])) {
    $military = $data['military'];
  } else {
    $military = '';
  }

  //Comment
  if (isset($data['questions'])) {
    $comment = $data['questions'];
  } else {
    $comment = '';
  }

  //city
  if (isset($data['city'])) {
    $city = $data['city'];
  } else {
    $city = '';
  }

  //address
  if (isset($data['address'])) {
    $address = $data['address'];
  } else {
    $address = '';
  }

  //country
  if (isset($data['country'])) {
    $country = $data['country'];
  } else {
    $country = '';
  }
  
  if($location == "no_preference" && sizeof($availableCampuses) == 1 && ($availableCampuses[0] == "Online" || $availableCampuses[0] == "online")){
    $location = "Online";
  }

  $url = "https://webforms.asu.edu/asu_rfi_dedupe/submit_rfi/$hashkey/$nodeid";

  $fields = array(
    'source_id' => $source,
    'base_url' => $domain,
    'prod_test_flag' => $form_mode,
    'first_name' => $fname,
    'last_name' => $lname,
    'prospect_level' => 'Undergraduate',
    'student_type' => $data['student_type'],
    'program' => $prog_code,
    'starting_semester' => $data['start_date'],
    'email' => $email,
    'zip' => $data['zipcode'],
    'phone' => $phone,
    'birth_date' => $dob,
    'country' => $country,
    'sms' => $mobile,
    'comments' => $comment,
    'prod_test_flag' => $form_mode,
    'street' => $address,
    'city' => $city,
    'state' => $state,
    'intnl_student' => $data['citizenship'],
    'veteran_status' => $military,
    'campus' => $campus,
    'location' => $location,
  );

  $body = json_encode($fields);

  if ($location == 'Online' || $location == 'online') {

    $asuOnlineList = asu_rfi_parse_programs();
    $managedOnline = asu_rfi_check_managed_online($prog_code, $asuOnlineList);

    if ($managedOnline == true) {
      $fields = asu_rfi_online_fields($fields, $nodeid, $ipAddress, $college_code, $type_of_form);

      $body = json_encode($fields);

      $fieldHash = sha1($mw_auth_key . '+' . $body);

      $url = "https://webforms.asu.edu/asu_edplus_middleware/submit/$fieldHash";
    }
  }

  try {
    $ch = curl_init();
    $options = array(
      CURLOPT_URL => $url,
      CURLOPT_POST => 1,
      CURLOPT_POSTFIELDS => $body,
      CURLOPT_RETURNTRANSFER => 1,
      CURLOPT_HTTPHEADER => array('Content-Type: text/json'),
      CURLOPT_SSLVERSION => 1,
      CURLOPT_SSL_VERIFYPEER => FALSE,
    );

    curl_setopt_array($ch, $options);

    $response = curl_exec($ch);
    $curl_code = curl_getinfo($ch);

    //If response from middleware is not a success message, save the error message submission node and send message onto the confirmation page screen
    // if($response == "Access Denied - hashed value does not match any registered site"){
    if (($curl_code['http_code'] == 403 || $curl_code['http_code'] == 500)) {
      $error_message = $response;
      $node_wrapper->field_rfi_error_message->set($error_message);
      $node_wrapper->save();
      $to_email = variable_get('asu_rfi_error_email', NULL); //get error reporting email address from rfi admin settings page
      //get middleware app key. If it's empty, don't send error email to admins.
      if (!empty($mw_auth_key)) {
        if (!empty($to_email)) {
          if (!isset($data['cron_run']) || ($data['cron_run'] != "Cron_running")) {
            $host = $_SERVER['HTTP_HOST'];
            //send email to admin reporting the issue
            $content = "The following <a href='https://$host/node/$nodeid'>submission</a> from " . $_SERVER['HTTP_HOST'] . " has some problem and hasn't been submitted to middleware";
            $subject = "Failed submissions to Sales Force from " . $_SERVER['HTTP_HOST'];
            $my_module = 'mimemail';
            $my_mail_token = '';
            $MimeMailSystem = drupal_mail_system($my_module, $my_mail_token);
            $default_from = variable_get('site_mail', ini_get('sendmail_from'));
            $message = array(
              'id' => $my_module . '_' . $my_mail_token,
              'from' => $default_from,
              'key' => '',
              'subject' => $subject,
              'body' => array($content),
              'to' => $to_email,
              'module' => 'mimemail',
              'headers' => array(
                'From' => $default_from,
                'Sender' => $default_from,
                'Return-Path' => $default_from,
                'Content-Type' => 'text/html; charset=UTF-8;',
                'to' => $to_email
              ),
            );

            $emailmessage = $MimeMailSystem->format($message);
            if ($MimeMailSystem->mail($emailmessage)) {
              watchdog('asu_rfi', 'SF Error message email sent to ' . $to_email);
              variable_set('SF_error_message_sent', time());
            }
          }
        }
      }

      if (drupal_valid_path('admin')) {
        drupal_set_message('Error message from Middleware: ' . $error_message);
      }

    }
    curl_close($ch);

    if (FALSE === $response) {
      throw new Exception(curl_error($ch), curl_errno($ch));
    }
  } catch (Exception $e) {
    trigger_error(sprintf(
      'Curl failed with error #%d: %s',
      $e->getCode(), $e->getMessage()),
      E_USER_ERROR);
  }
}

function asu_rfi_check_managed_online($program, $getProgramsList)
{
  foreach($getProgramsList as $onlineItem){
    if($onlineItem->plancode == $program && $onlineItem->crmdestination !== "asu"){
      return true;
    }
  }
  return false;
}

function asu_rfi_correct_campus_idu($campus)
{

  switch ($campus) {
    case "Tempe":
      $campusId = "TEMPE";
      break;
    case "Online":
      $campusId = "ONLNE";
      break;
    case "East Arizona College":
      $campusId = "EAC";
      break;
    case "Polytechnic":
      $campusId = "POLY";
      break;
    case "Lake Havasu":
      $campusId = "CALHC";
      break;
    case "West":
      $campusId = "WEST";
      break;
    case "Downtown":
      $campusId = "DTPHX";
      break;
    default:
      $campusId = '';

  }

  return $campusId;
}

function asu_rfi_online_fields($fields, $nodeid, $ip, $college_code, $type_of_form)
{

  $fieldsUpdated = array(
    'first_name' => $fields['first_name'],
    'last_name' => $fields['last_name'],
    'program' => $college_code . '-' . $fields['program'],
    'email' => $fields['email'],
    'phone' => $fields['phone'],
    'prod_test_flag' => $fields['prod_test_flag'],
    'base_url' => $fields['base_url'],
    'remote_nid' => $nodeid,
    'ldetect_v' => $ip,
    'campaign' => $fields['base_url'] . '-' . $type_of_form . '-UG',
    'sourceid' => $nodeid,
  );

  return $fieldsUpdated;
}
