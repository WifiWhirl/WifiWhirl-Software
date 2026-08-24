import type { Ref } from 'preact';
import { useId } from 'preact/hooks';
import { NumberInput, Row, Toggle } from './ui';

/**
 * @brief Labelled text input row.
 */
export function TextField(props: {
  label: string; value: string; onInput: (v: string) => void;
  type?: string; placeholder?: string; maxLength?: number; inputMode?: 'text' | 'numeric' | 'decimal' | 'tel' | 'email' | 'url';
  inputRef?: Ref<HTMLInputElement>;
}) {
  const id = useId();
  return (
    <Row label={props.label} htmlFor={id}>
      <input
        ref={props.inputRef}
        id={id}
        type={props.type || 'text'}
        value={props.value}
        placeholder={props.placeholder}
        maxLength={props.maxLength}
        inputMode={props.inputMode}
        onInput={(e) => props.onInput((e.target as HTMLInputElement).value)}
      />
    </Row>
  );
}

/**
 * @brief Labelled numeric input row backed by NumberInput.
 */
export function NumField(props: {
  label: string; value: number | string; onInput: (v: string) => void;
  min?: number; max?: number; step?: number; unit?: string;
}) {
  const id = useId();
  return (
    <Row label={props.label} htmlFor={id}>
      <NumberInput
        id={id}
        value={props.value}
        min={props.min}
        max={props.max}
        step={props.step}
        unit={props.unit}
        onInput={props.onInput}
      />
    </Row>
  );
}

/**
 * @brief Labelled boolean toggle row.
 */
export function CheckField(props: { label: string; checked: boolean; onChange: (v: boolean) => void; disabled?: boolean }) {
  const id = useId();
  return (
    <Row label={props.label} htmlFor={id}>
      <Toggle id={id} checked={props.checked} disabled={props.disabled} onChange={props.onChange} />
    </Row>
  );
}
