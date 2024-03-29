<?php

/**
 * A result of a parsing stage.
 */
class FeedsParserResult extends FeedsResult {
  public $title;
  public $description;
  public $link;
  public $items;
  public $current_item;

  /**
   * Constructor.
   */
  public function __construct($items = array()) {
    $this->title = '';
    $this->description = '';
    $this->link = '';
    $this->items = $items;
  }

  /**
   * @todo Move to a nextItem() based approach, not consuming the item array.
   *   Can only be done once we don't cache the entire batch object between page
   *   loads for batching anymore.
   *
   * @return
   *   Next available item or NULL if there is none. Every returned item is
   *   removed from the internal array.
   */
  public function shiftItem() {
    $this->current_item = array_shift($this->items);
    return $this->current_item;
  }

  /**
   * @return
   *   Current result item.
   */
  public function currentItem() {
    return empty($this->current_item) ? NULL : $this->current_item;
  }
}

/**
 * Abstract class, defines interface for parsers.
 */
abstract class FeedsParser extends FeedsPlugin {

  /**
   * Parse content fetched by fetcher.
   *
   * Extending classes must implement this method.
   *
   * @param FeedsSource $source
   *   Source information.
   * @param $fetcher_result
   *   FeedsFetcherResult returned by fetcher.
   */
  public abstract function parse(FeedsSource $source, FeedsFetcherResult $fetcher_result);

  /**
   * Clear all caches for results for given source.
   *
   * @param FeedsSource $source
   *   Source information for this expiry. Implementers can choose to only clear
   *   caches pertaining to this source.
   */
  public function clear(FeedsSource $source) {}

  /**
   * Declare the possible mapping sources that this parser produces.
   *
   * @ingroup mappingapi
   *
   * @return
   *   An array of mapping sources, or FALSE if the sources can be defined by
   *   typing a value in a text field.
   *
   *   Example:
   *   @code
   *   array(
   *     'title' => t('Title'),
   *     'created' => t('Published date'),
   *     'url' => t('Feed item URL'),
   *     'guid' => t('Feed item GUID'),
   *   )
   *   @endcode
   */
  public function getMappingSources() {
    self::loadMappers();
    $sources = array();
    feeds_alter('feeds_parser_sources', $sources, feeds_importer($this->id)->config['content_type']);
    if (!feeds_importer($this->id)->config['content_type']) {
      return $sources;
    }
    $sources['parent:uid'] = array(
      'name' => t('Feed node: User ID'),
      'description' => t('The feed node author uid.'),
    );
    return $sources;
  }

  /**
   * Get an element identified by $element_key of the given item.
   * The element key corresponds to the values in the array returned by
   * FeedsParser::getMappingSources().
   *
   * This method is invoked from FeedsProcessor::map() when a concrete item is
   * processed.
   *
   * @ingroup mappingapi
   *
   * @param $batch
   *   FeedsImportBatch object containing the sources to be mapped from.
   * @param $element_key
   *   The key identifying the element that should be retrieved from $source
   *
   * @return
   *   The source element from $item identified by $element_key.
   *
   * @see FeedsProcessor::map()
   * @see FeedsCSVParser::getSourceElement().
   */
  public function getSourceElement(FeedsSource $source, FeedsParserResult $result, $element_key) {
    if ($element_key == 'parent:uid' &&
        $source->feed_nid &&
        ($node = node_load($source->feed_nid))) {
      return $node->uid;
    }
    $item = $result->currentItem();
    return isset($item[$element_key]) ? $item[$element_key] : '';
  }
}

/**
 * Defines an element of a parsed result. Such an element can be a simple type,
 * a complex type (derived from FeedsElement) or an array of either.
 *
 * @see FeedsEnclosure
 */
class FeedsElement {
  // The standard value of this element. This value can contain be a simple type,
  // a FeedsElement or an array of either.
  protected $value;

  /**
   * Constructor.
   */
  public function __construct($value) {
    $this->value = $value;
  }

  /**
   * @todo Make value public and deprecate use of getValue().
   *
   * @return
   *   Value of this FeedsElement represented as a scalar.
   */
  public function getValue() {
    return $this->value;
  }

  /**
   * Magic method __toString() for printing and string conversion of this
   * object.
   *
   * @return
   *   A string representation of this element.
   */
  public function __toString() {
    if (is_array($this->value)) {
      return 'Array';
    }
    if (is_object($this->value)) {
      return 'Object';
    }
    return (string) $this->getValue();
  }
}

/**
 * Encapsulates a taxonomy style term object.
 *
 * Objects of this class can be turned into a taxonomy term style arrays by
 * casting them.
 *
 * @code
 *   $term_object = new FeedsTermElement($term_array);
 *   $term_array = (array)$term_object;
 * @endcode
 */
class FeedsTermElement extends FeedsElement {
  public $tid, $vid, $name;

  /**
   * @param $term
   *   An array or a stdClass object that is a Drupal taxonomy term.
   */
  public function __construct($term) {
    if (is_array($term)) {
      parent::__construct($term['name']);
      foreach ($this as $key => $value) {
        $this->$key = isset($term[$key]) ? $term[$key] : NULL;
      }
    }
    elseif (is_object($term)) {
      parent::__construct($term->name);
      foreach ($this as $key => $value) {
        $this->$key = isset($term->$key) ? $term->$key : NULL;
      }
    }
  }

  /**
   * Use $name as $value.
   */
  public function getValue() {
    return $this->name;
  }
}

/**
 * A geo term element.
 */
class FeedsGeoTermElement extends FeedsTermElement {
  public $lat, $lon, $bound_top, $bound_right, $bound_bottom, $bound_left, $geometry;
  /**
   * @param $term
   *   An array or a stdClass object that is a Drupal taxonomy term. Can include
   *   geo extensions.
   */
  public function __construct($term) {
    parent::__construct($term);
  }
}

/**
 * Enclosure element, can be part of the result array.
 */
class FeedsEnclosure extends FeedsElement {
  protected $mime_type;

  /**
   * Constructor, requires MIME type.
   *
   * @param $value
   *   A path to a local file or a URL to a remote document.
   * @param $mimetype
   *   The mime type of the resource.
   */
  public function __construct($value, $mime_type) {
    parent::__construct($value);
    $this->mime_type = $mime_type;
  }

  /**
   * @return
   *   MIME type of return value of getValue().
   */
  public function getMIMEType() {
    return $this->mime_type;
  }

  /**
   * Use this method instead of FeedsElement::getValue() when fetching the file
   * from the URL.
   *
   * @return
   *   Value with encoded space characters to safely fetch the file from the URL.
   *
   * @see FeedsElement::getValue()
   */
  public function getUrlEncodedValue() {
    return str_replace(' ', '%20', $this->getValue());
  }

  /**
   * Use this method instead of FeedsElement::getValue() to get the file name
   * transformed for better local saving (underscores instead of spaces)
   *
   * @return
   *   Value with space characters changed to underscores.
   *
   * @see FeedsElement::getValue()
   */
  public function getLocalValue() {
    return str_replace(' ', '_', $this->getValue());
  }

  /**
   * @return
   *   The content of the referenced resource.
   */
  public function getContent() {
    feeds_include_library('http_request.inc', 'http_request');
    $result = http_request_get($this->getUrlEncodedValue());
    if ($result->code != 200) {
      throw new Exception(t('Download of @url failed with code !code.', array('@url' => $this->getUrlEncodedValue(), '!code' => $result->code)));
    }
    return $result->data;
  }

  /**
   * Get a Drupal file object of the enclosed resource, download if necessary.
   *
   * @param $destination
   *   The path or uri specifying the target directory in which the file is
   *   expected. Don't use trailing slashes unless it's a streamwrapper scheme.
   *
   * @return
   *   A Drupal temporary file object of the enclosed resource.
   *
   * @throws Exception
   *   If file object could not be created.
   */
  public function getFile($destination) {

    if ($this->getValue()) {
      // Prepare destination directory.
      file_prepare_directory($destination, FILE_MODIFY_PERMISSIONS | FILE_CREATE_DIRECTORY);
      // Copy or save file depending on whether it is remote or local.
      if (drupal_realpath($this->getValue())) {
        $file           = new stdClass();
        $file->uid      = 0;
        $file->uri      = $this->getValue();
        $file->filemime = $this->mime_type;
        $file->filename = basename($file->uri);
        if (dirname($file->uri) != $destination) {
          $file = file_copy($file, $destination);
        }
        else {
          // If file is not to be copied, check whether file already exists,
          // as file_save() won't do that for us (compare file_copy() and
          // file_save())
          $existing_files = file_load_multiple(array(), array('uri' => $file->uri));
          if (count($existing_files)) {
            $existing = reset($existing_files);
            $file->fid = $existing->fid;
            $file->filename = $existing->filename;
          }
          file_save($file);
        }
      }
      else {
        $filename = basename($this->getLocalValue());
        if (module_exists('transliteration')) {
          require_once drupal_get_path('module', 'transliteration') . '/transliteration.inc';
          $filename = transliteration_clean_filename($filename);
        }
        if(file_uri_target($destination)) {
          $destination .= trim($destination, '/') . '/';
        }
        $file = file_save_data($this->getContent(), $destination . $filename);
      }

      // We couldn't make sense of this enclosure, throw an exception.
      if (!$file) {
        throw new Exception(t('Invalid enclosure %enclosure', array('%enclosure' => $this->getValue())));
      }
    }
    return $file;
  }
}

/**
 * Defines a date element of a parsed result (including ranges, repeat).
 */
class FeedsDateTimeElement extends FeedsElement {

  // Start date and end date.
  public $start;
  public $end;

  /**
   * Constructor.
   *
   * @param $start
   *   A FeedsDateTime object or a date as accepted by FeedsDateTime.
   * @param $end
   *   A FeedsDateTime object or a date as accepted by FeedsDateTime.
   * @param $tz
   *   A PHP DateTimeZone object.
   */
  public function __construct($start = NULL, $end = NULL, $tz = NULL) {
    $this->start = (!isset($start) || ($start instanceof FeedsDateTime)) ? $start : new FeedsDateTime($start, $tz);
    $this->end = (!isset($end) || ($end instanceof FeedsDateTime)) ? $end : new FeedsDateTime($end, $tz);
  }

  /**
   * Override FeedsElement::getValue().
   *
   * @return
   *   The UNIX timestamp of this object's start date. Return value is
   *   technically a string but will only contain numeric values.
   */
  public function getValue() {
    if ($this->start) {
      return $this->start->format('U');
    }
    return '0';
  }

  /**
   * Merge this field with another. Most stuff goes down when merging the two
   * sub-dates.
   *
   * @see FeedsDateTime
   */
  public function merge(FeedsDateTimeElement $other) {
    $this2 = clone $this;
    if ($this->start && $other->start) {
      $this2->start = $this->start->merge($other->start);
    }
    elseif ($other->start) {
      $this2->start = clone $other->start;
    }
    elseif ($this->start) {
      $this2->start = clone $this->start;
    }

    if ($this->end && $other->end) {
      $this2->end = $this->end->merge($other->end);
    }
    elseif ($other->end) {
      $this2->end = clone $other->end;
    }
    elseif ($this->end) {
      $this2->end = clone $this->end;
    }
    return $this2;
  }

  /**
   * Helper method for buildDateField(). Build a FeedsDateTimeElement object
   * from a standard formatted node.
   */
  protected static function readDateField($entity, $field_name) {
    $ret = new FeedsDateTimeElement();
    if (isset($entity->{$field_name}['und'][0]['date']) && $entity->{$field_name}['und'][0]['date'] instanceof FeedsDateTime) {
      $ret->start = $entity->{$field_name}['und'][0]['date'];
    }
    if (isset($entity->{$field_name}['und'][0]['date2']) && $entity->{$field_name}['und'][0]['date2'] instanceof FeedsDateTime) {
      $ret->end = $entity->{$field_name}['und'][0]['date2'];
    }
    return $ret;
  }

  /**
   * Build a entity's date field from our object.
   *
   * @param $entity
   *   The entity to build the date field on.
   * @param $field_name
   *   The name of the field to build.
   */
  public function buildDateField($entity, $field_name) {
    $info = field_info_field($field_name);

    $oldfield = FeedsDateTimeElement::readDateField($entity, $field_name);
    // Merge with any preexisting objects on the field; we take precedence.
    $oldfield = $this->merge($oldfield);
    $use_start = $oldfield->start;
    $use_end = $oldfield->end;

    // Set timezone if not already in the FeedsDateTime object
    $to_tz = date_get_timezone($info['settings']['tz_handling'], date_default_timezone());
    $temp = new FeedsDateTime(NULL, new DateTimeZone($to_tz));

    $db_tz = '';
    if ($use_start) {
      $use_start = $use_start->merge($temp);
      if (!date_timezone_is_valid($use_start->getTimezone()->getName())) {
        $use_start->setTimezone(new DateTimeZone("UTC"));
      }
      $db_tz = date_get_timezone_db($info['settings']['tz_handling'], $use_start->getTimezone()->getName());
    }
    if ($use_end) {
      $use_end = $use_end->merge($temp);
      if (!date_timezone_is_valid($use_end->getTimezone()->getName())) {
        $use_end->setTimezone(new DateTimeZone("UTC"));
      }
      if (!$db_tz) {
        $db_tz = date_get_timezone_db($info['settings']['tz_handling'], $use_end->getTimezone()->getName());
      }
    }
    if (!$db_tz) {
      return;
    }

    $db_tz = new DateTimeZone($db_tz);
    if (!isset($entity->{$field_name})) {
      $entity->{$field_name} = array('und' => array());
    }
    if ($use_start) {
      $entity->{$field_name}['und'][0]['timezone'] = $use_start->getTimezone()->getName();
      $entity->{$field_name}['und'][0]['offset'] = $use_start->getOffset();
      $use_start->setTimezone($db_tz);
      $entity->{$field_name}['und'][0]['date'] = $use_start;
      /**
       * @todo the date_type_format line could be simplified based upon a patch
       *   DO issue #259308 could affect this, follow up on at some point.
       *   Without this, all granularity info is lost.
       *   $use_start->format(date_type_format($field['type'], $use_start->granularity));
       */
      $entity->{$field_name}['und'][0]['value'] = $use_start->format(date_type_format($info['type']));
    }
    if ($use_end) {
      // Don't ever use end to set timezone (for now)
      $entity->{$field_name}['und'][0]['offset2'] = $use_end->getOffset();
      $use_end->setTimezone($db_tz);
      $entity->{$field_name}['und'][0]['date2'] = $use_end;
      $entity->{$field_name}['und'][0]['value2'] = $use_end->format(date_type_format($info['type']));
    }
  }
}

/**
 * Extend PHP DateTime class with granularity handling, merge functionality and
 * slightly more flexible initialization parameters.
 *
 * This class is a Drupal independent extension of the >= PHP 5.2 DateTime
 * class.
 *
 * @see FeedsDateTimeElement class
 */
class FeedsDateTime extends DateTime {
  public $granularity = array();
  protected static $allgranularity = array('year', 'month', 'day', 'hour', 'minute', 'second', 'zone');
  private $_serialized_time;
  private $_serialized_timezone;

  /**
   * Helper function to prepare the object during serialization.
   *
   * We are extending a core class and core classes cannot be serialized.
   *
   * Ref: http://bugs.php.net/41334, http://bugs.php.net/39821
   */
  public function __sleep() {
    $this->_serialized_time = $this->format('c');
    $this->_serialized_timezone = $this->getTimezone()->getName();
    return array('_serialized_time', '_serialized_timezone');
  }

  /**
   * Upon unserializing, we must re-build ourselves using local variables.
   */
  public function __wakeup() {
    $this->__construct($this->_serialized_time, new DateTimeZone($this->_serialized_timezone));
  }

  /**
   * Overridden constructor.
   *
   * @param $time
   *   time string, flexible format including timestamp. Invalid formats will
   *   fall back to 'now'.
   * @param $tz
   *   PHP DateTimeZone object, NULL allowed
   */
  public function __construct($time = '', $tz = NULL) {
    // Assume UNIX timestamp if numeric.
    if (is_numeric($time)) {
      // Make sure it's not a simple year
      if ((is_string($time) && strlen($time) > 4) || is_int($time)) {
        $time = "@" . $time;
      }
    }

    // PHP < 5.3 doesn't like the GMT- notation for parsing timezones.
    $time = str_replace("GMT-", "-", $time);
    $time = str_replace("GMT+", "+", $time);

    // Some PHP 5.2 version's DateTime class chokes on invalid dates.
    if (!strtotime($time)) {
      $time = 'now';
    }

    // Create and set time zone separately, PHP 5.2.6 does not respect time zone
    // argument in __construct().
    parent::__construct($time);
    $time_arr = date_parse($time);
    if (!isset($tz)) {

      if (isset($time_arr['tz_abbr'])) {
        $tz_param = $time_arr['tz_abbr'];
      } elseif (isset($time_arr['tz_id'])) {
        $tz_param = $time_arr['tz_id'];
      }
      if($tz_param === 'Z'){
        $tz_param = 'UTC';
      }
      $tz = new DateTimeZone($tz_param);
      unset($tz_param);
    }
    $this->setTimeZone($tz);

    // Verify that timezone has not been specified as an offset.
    if (!preg_match('/[a-zA-Z]/', $this->getTimezone()->getName())) {
      $this->setTimezone(new DateTimeZone("UTC"));
    }

    // Finally set granularity.
    $this->setGranularityFromTime($time, $tz);
  }

  /**
   * This function will keep this object's values by default.
   */
  public function merge(FeedsDateTime $other) {
    $other_tz = $other->getTimezone();
    $this_tz = $this->getTimezone();
    // Figure out which timezone to use for combination.
    $use_tz = ($this->hasGranularity('zone') || !$other->hasGranularity('zone')) ? $this_tz : $other_tz;

    $this2 = clone $this;
    $this2->setTimezone($use_tz);
    $other->setTimezone($use_tz);
    $val = $this2->toArray();
    $otherval = $other->toArray();
    foreach (self::$allgranularity as $g) {
      if ($other->hasGranularity($g) && !$this2->hasGranularity($g)) {
        // The other class has a property we don't; steal it.
        $this2->addGranularity($g);
        $val[$g] = $otherval[$g];
      }
    }
    $other->setTimezone($other_tz);

    $this2->setDate($val['year'], $val['month'], $val['day']);
    $this2->setTime($val['hour'], $val['minute'], $val['second']);
    return $this2;
  }

  /**
   * Overrides default DateTime function. Only changes output values if
   * actually had time granularity. This should be used as a "converter" for
   * output, to switch tzs.
   *
   * In order to set a timezone for a datetime that doesn't have such
   * granularity, merge() it with one that does.
   */
  public function setTimezone($tz, $force = FALSE) {
    // PHP 5.2.6 has a fatal error when setting a date's timezone to itself.
    // http://bugs.php.net/bug.php?id=45038
    if (version_compare(PHP_VERSION, '5.2.7', '<') && $tz == $this->getTimezone()) {
      $tz = new DateTimeZone($tz->getName());
    }

    if (!$this->hasTime() || !$this->hasGranularity('zone') || $force) {
      // this has no time or timezone granularity, so timezone doesn't mean much
      // We set the timezone using the method, which will change the day/hour, but then we switch back
      $arr = $this->toArray();
      parent::setTimezone($tz);
      $this->setDate($arr['year'], $arr['month'], $arr['day']);
      $this->setTime($arr['hour'], $arr['minute'], $arr['second']);
      return;
    }
    parent::setTimezone($tz);
  }

  /**
   * Safely adds a granularity entry to the array.
   */
  public function addGranularity($g) {
    $this->granularity[] = $g;
    $this->granularity = array_unique($this->granularity);
  }

  /**
   * Removes a granularity entry from the array.
   */
  public function removeGranularity($g) {
    if ($key = array_search($g, $this->granularity)) {
      unset($this->granularity[$key]);
    }
  }

  /**
   * Checks granularity array for a given entry.
   */
  public function hasGranularity($g) {
    return in_array($g, $this->granularity);
  }

  /**
   * Returns whether this object has time set. Used primarily for timezone
   * conversion and fomratting.
   *
   * @todo currently very simplistic, but effective, see usage
   */
  public function hasTime() {
    return $this->hasGranularity('hour');
  }

  /**
   * Protected function to find the granularity given by the arguments to the
   * constructor.
   */
  protected function setGranularityFromTime($time, $tz) {
    $this->granularity = array();
    $temp = date_parse($time);
    // This PHP method currently doesn't have resolution down to seconds, so if
    // there is some time, all will be set.
    foreach (self::$allgranularity AS $g) {
      if ((isset($temp[$g]) && is_numeric($temp[$g])) || ($g == 'zone' && (isset($temp['zone_type']) && $temp['zone_type'] > 0))) {
        $this->granularity[] = $g;
      }
    }
    if ($tz) {
      $this->addGranularity('zone');
    }
  }

  /**
   * Helper to return all standard date parts in an array.
   */
  protected function toArray() {
    return array('year' => $this->format('Y'), 'month' => $this->format('m'), 'day' => $this->format('d'), 'hour' => $this->format('H'), 'minute' => $this->format('i'), 'second' => $this->format('s'), 'zone' => $this->format('e'));
  }
}

/**
 * Converts to UNIX time.
 *
 * @param $date
 *   A date that is either a string, a FeedsDateTimeElement or a UNIX timestamp.
 * @param $default_value
 *   A default UNIX timestamp to return if $date could not be parsed.
 *
 * @return
 *   $date as UNIX time if conversion was successful, $dfeault_value otherwise.
 */
function feeds_to_unixtime($date, $default_value) {
  if (is_numeric($date)) {
    return $date;
  }
  elseif (is_string($date)) {
    $date = new FeedsDateTimeElement($date);
    return $date->getValue();
  }
  elseif ($date instanceof FeedsDateTimeElement) {
    return $date->getValue();
  }
  return $default_value;
}
