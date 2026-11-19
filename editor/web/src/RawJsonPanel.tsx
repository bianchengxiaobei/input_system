interface RawJsonPanelProps {
  value: string;
  error: string;
  dirty: boolean;
  onValueChange: (value: string) => void;
  onApply: () => void;
  onFormat: () => void;
  onRevert: () => void;
}

export default function RawJsonPanel({
  value,
  error,
  dirty,
  onValueChange,
  onApply,
  onFormat,
  onRevert,
}: RawJsonPanelProps) {
  return (
    <div className="raw-panel">
      <div className="raw-toolbar">
        <span className={dirty ? "dirty" : "subtle"}>
          {dirty ? "unapplied JSON changes" : "JSON matches the editor"}
        </span>
        <div className="raw-actions">
          <button type="button" className="small-button" onClick={onFormat} disabled={!value}>
            Format
          </button>
          <button
            type="button"
            className="small-button"
            onClick={onRevert}
            disabled={!dirty}
          >
            Revert
          </button>
          <button
            type="button"
            className="small-button primary-small"
            onClick={onApply}
            disabled={!dirty}
          >
            Apply JSON
          </button>
        </div>
      </div>
      {error ? <div className="raw-error">{error}</div> : null}
      <textarea
        className="json-editor"
        value={value}
        onChange={(event) => onValueChange(event.target.value)}
        placeholder={'{\n  "version": 1,\n  "maps": []\n}'}
        spellCheck={false}
      />
    </div>
  );
}
